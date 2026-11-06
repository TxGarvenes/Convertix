#!/usr/bin/env python3
"""
Empaquete les outils CLI Windows (bin/ffmpeg, bin/imagemagick, bin/pandoc, bin/vtracer)
directement dans un PE Overlay compressé (DEFLATE) à la fin de Convertix.exe.
Permet de distribuer Convertix sous la forme d'un SEUL fichier Convertix.exe autonome.
"""

import hashlib
import pathlib
import struct
import sys
import zlib

MAGIC = b"CVTXPK01"
FOOTER_STRUCT = struct.Struct("<8sQQ")  # magic (8B), toc_offset (uint64), bundle_hash (uint64)

# Seuls les binaires et fichiers de configuration réellement utilisés par Convertix sont embarqués
ALLOWED_FILES = {
    "vtracer": lambda p: p.name.lower() == "vtracer.exe",
    "ffmpeg": lambda p: p.name.lower() in ("ffmpeg.exe", "ffprobe.exe"),
    "pandoc": lambda p: p.name.lower() == "pandoc.exe",
    "imagemagick": lambda p: p.name.lower() == "magick.exe" or p.suffix.lower() in (".xml", ".icc"),
}


def strip_existing_overlay(exe_path: pathlib.Path) -> None:
    size = exe_path.stat().st_size
    if size < FOOTER_STRUCT.size:
        return

    with exe_path.open("rb+") as f:
        f.seek(size - FOOTER_STRUCT.size)
        footer_bytes = f.read(FOOTER_STRUCT.size)
        magic, toc_offset, _ = FOOTER_STRUCT.unpack(footer_bytes)
        if magic != MAGIC:
            return

        # Lire le premier offset du TOC pour retrouver le début de l'overlay
        f.seek(toc_offset)
        (entry_count,) = struct.unpack("<I", f.read(4))
        first_offset = toc_offset
        for _ in range(entry_count):
            (grp_len,) = struct.unpack("<I", f.read(4))
            f.seek(grp_len, 1)
            (path_len,) = struct.unpack("<I", f.read(4))
            f.seek(path_len, 1)
            offset_in_exe, _, _ = struct.unpack("<QQQ", f.read(24))
            if offset_in_exe < first_offset:
                first_offset = offset_in_exe

        print(f"[pack_windows_exe] Suppression de l'ancien overlay a l'offset {first_offset}...")
        f.truncate(first_offset)


def main() -> int:
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <Convertix.exe> <bin_dir>")
        return 1

    exe_path = pathlib.Path(sys.argv[1]).resolve()
    bin_dir = pathlib.Path(sys.argv[2]).resolve()

    if not exe_path.is_file():
        print(f"Erreur: {exe_path} introuvable.", file=sys.stderr)
        return 1

    strip_existing_overlay(exe_path)

    files_to_pack = []
    for group_name, predicate in ALLOWED_FILES.items():
        group_dir = bin_dir / group_name
        if not group_dir.is_dir():
            continue
        for item in sorted(group_dir.iterdir()):
            if item.is_file() and predicate(item):
                rel_path = f"bin/{group_name}/{item.name}"
                files_to_pack.append((group_name, rel_path, item))

    if not files_to_pack:
        print("[pack_windows_exe] Aucun outil Windows (.exe) trouve dans bin/, overlay ignore.")
        return 0

    hasher = hashlib.sha256()
    toc_entries = []

    with exe_path.open("ab") as exe_file:
        for group_name, rel_path, src_path in files_to_pack:
            raw_bytes = src_path.read_bytes()
            uncompressed_size = len(raw_bytes)

            compressor = zlib.compressobj(level=6, method=zlib.DEFLATED, wbits=-15)
            compressed_bytes = compressor.compress(raw_bytes) + compressor.flush()
            compressed_size = len(compressed_bytes)

            offset_in_exe = exe_file.tell()
            exe_file.write(compressed_bytes)

            hasher.update(rel_path.encode("utf-8"))
            hasher.update(struct.pack("<QQ", uncompressed_size, compressed_size))

            toc_entries.append((group_name, rel_path, offset_in_exe, compressed_size, uncompressed_size))
            ratio = (compressed_size / uncompressed_size * 100.0) if uncompressed_size > 0 else 100.0
            print(
                f"  + {rel_path:<36} : {uncompressed_size / (1024*1024):6.2f} Mo -> "
                f"{compressed_size / (1024*1024):6.2f} Mo ({ratio:4.1f}%)"
            )

        toc_offset = exe_file.tell()
        exe_file.write(struct.pack("<I", len(toc_entries)))
        for group_name, rel_path, offset_in_exe, compressed_size, uncompressed_size in toc_entries:
            grp_bytes = group_name.encode("utf-8")
            path_bytes = rel_path.encode("utf-8")
            exe_file.write(struct.pack("<I", len(grp_bytes)))
            exe_file.write(grp_bytes)
            exe_file.write(struct.pack("<I", len(path_bytes)))
            exe_file.write(path_bytes)
            exe_file.write(struct.pack("<QQQ", offset_in_exe, compressed_size, uncompressed_size))

        bundle_hash = struct.unpack("<Q", hasher.digest()[:8])[0]
        exe_file.write(FOOTER_STRUCT.pack(MAGIC, toc_offset, bundle_hash))

    final_size_mb = exe_path.stat().st_size / (1024 * 1024)
    print(f"[pack_windows_exe] Termine ! Executable tout-en-un : {exe_path} ({final_size_mb:.2f} Mo)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
