#include "FileDetector.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>

namespace {

std::string getLowerExtension(const std::filesystem::path& filePath) {
    std::string ext = filePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

bool isRawExtension(const std::string& ext) {
    return ext == ".dng" || ext == ".cr2" || ext == ".nef" || ext == ".arw";
}

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

FileType FileDetector::detect(const std::filesystem::path& filePath) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec) || !std::filesystem::is_regular_file(filePath, ec)) {
        return FileType::UNKNOWN;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return FileType::UNKNOWN;
    }

    std::array<std::uint8_t, 512> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto bytesRead = file.gcount();

    const std::string ext = getLowerExtension(filePath);

    if (bytesRead < 2) {
        if (ext == ".txt") return FileType::TXT;
        if (ext == ".md" || ext == ".markdown") return FileType::MD;
        if (ext == ".html" || ext == ".htm") return FileType::HTML;
        return FileType::UNKNOWN;
    }

    const std::string headerAscii = toLowerAscii(
        std::string(reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(bytesRead))
    );

    // 1. PNG: 89 50 4E 47 0D 0A 1A 0A (8 bytes)
    if (bytesRead >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
        return FileType::PNG;
    }

    // 2. JPEG: FF D8 FF (3 bytes)
    if (bytesRead >= 3 &&
        bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return FileType::JPEG;
    }

    // 3. Conteneurs RIFF: WEBP, WAV, AVI (52 49 46 46 à 0..3)
    if (bytesRead >= 12 &&
        bytes[0] == 0x52 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x46) {
        // WEBP: 57 45 42 50 ("WEBP") à 8..11
        if (bytes[8] == 0x57 && bytes[9] == 0x45 && bytes[10] == 0x42 && bytes[11] == 0x50) {
            return FileType::WEBP;
        }
        // WAV: 57 41 56 45 ("WAVE") à 8..11
        if (bytes[8] == 0x57 && bytes[9] == 0x41 && bytes[10] == 0x56 && bytes[11] == 0x45) {
            return FileType::WAV;
        }
        // AVI: 41 56 49 20 ("AVI ") à 8..11
        if (bytes[8] == 0x41 && bytes[9] == 0x56 && bytes[10] == 0x49 && bytes[11] == 0x20) {
            return FileType::AVI;
        }
    }

    // 4. Conteneur EBML (MKV / WEBM): 1A 45 DF A3 à 0..3
    if (bytesRead >= 4 &&
        bytes[0] == 0x1A && bytes[1] == 0x45 && bytes[2] == 0xDF && bytes[3] == 0xA3) {
        if (headerAscii.find("webm") != std::string::npos) {
            return FileType::WEBM;
        }
        if (headerAscii.find("matroska") != std::string::npos) {
            return FileType::MKV;
        }
        if (ext == ".webm") {
            return FileType::WEBM;
        }
        return FileType::MKV;
    }

    // 5. Conteneurs ISO Base Media ("ftyp" à 4..7): HEIC, M4A, MP4, MOV
    if (bytesRead >= 12 &&
        bytes[4] == 'f' && bytes[5] == 't' && bytes[6] == 'y' && bytes[7] == 'p') {
        const std::string brand(reinterpret_cast<const char*>(&bytes[8]), 4);
        if (brand == "heic" || brand == "heix" || brand == "mif1" || brand == "msf1" ||
            brand == "hevc" || brand == "hevx") {
            return FileType::HEIC;
        }
        if (brand == "M4A " || brand == "M4B " || brand == "m4a ") {
            return FileType::M4A;
        }
        if (brand == "qt  ") {
            return FileType::MOV;
        }
        if (ext == ".mov") {
            return FileType::MOV;
        }
        if (ext == ".m4a") {
            return FileType::M4A;
        }
        return FileType::MP4;
    }

    // Atomes QuickTime sans "ftyp" (moov, wide, free, mdat aux octets 4..7)
    if (bytesRead >= 8) {
        const std::string atom(reinterpret_cast<const char*>(&bytes[4]), 4);
        if (atom == "moov" || atom == "wide" || atom == "free" || atom == "mdat") {
            if (ext == ".mp4") {
                return FileType::MP4;
            }
            return FileType::MOV;
        }
    }

    // 6. FLAC: 66 4C 61 43 ("fLaC")
    if (bytesRead >= 4 &&
        bytes[0] == 0x66 && bytes[1] == 0x4C && bytes[2] == 0x61 && bytes[3] == 0x43) {
        return FileType::FLAC;
    }

    // 7. OGG: 4F 67 67 53 ("OggS")
    if (bytesRead >= 4 &&
        bytes[0] == 0x4F && bytes[1] == 0x67 && bytes[2] == 0x67 && bytes[3] == 0x53) {
        return FileType::OGG;
    }

    // 8. MP3: ID3v2 ("ID3" = 49 44 33)
    if (bytesRead >= 3 &&
        bytes[0] == 0x49 && bytes[1] == 0x44 && bytes[2] == 0x33) {
        return FileType::MP3;
    }

    // 9. AAC (ADTS: FF F1 ou FF F9)
    if (bytesRead >= 2 &&
        bytes[0] == 0xFF && (bytes[1] == 0xF1 || bytes[1] == 0xF9)) {
        return FileType::AAC;
    }

    // 10. MP3 sans tag ID3 (Frame sync MPEG Audio: FF FB, FF F3, FF F2, FF FA, FF E3)
    if (bytesRead >= 2 &&
        bytes[0] == 0xFF &&
        (bytes[1] == 0xFB || bytes[1] == 0xF3 || bytes[1] == 0xF2 || bytes[1] == 0xFA || bytes[1] == 0xE3)) {
        return FileType::MP3;
    }

    // 11. GIF: 47 49 46 38 37 61 (GIF87a) ou 47 49 46 38 39 61 (GIF89a)
    if (bytesRead >= 6 &&
        bytes[0] == 0x47 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x38 &&
        (bytes[4] == 0x37 || bytes[4] == 0x39) && bytes[5] == 0x61) {
        return FileType::GIF;
    }

    // 12. TIFF / DNG / RAW: 49 49 2A 00 (Little-Endian) ou 4D 4D 00 2A (Big-Endian)
    if (bytesRead >= 4 &&
        ((bytes[0] == 0x49 && bytes[1] == 0x49 && bytes[2] == 0x2A && bytes[3] == 0x00) ||
         (bytes[0] == 0x4D && bytes[1] == 0x4D && bytes[2] == 0x00 && bytes[3] == 0x2A))) {
        if ((bytesRead >= 10 && bytes[8] == 0x43 && bytes[9] == 0x52) || isRawExtension(ext)) {
            return FileType::RAW;
        }
        return FileType::TIFF;
    }

    // 13. ICO: 00 00 01 00
    if (bytesRead >= 4 &&
        bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x01 && bytes[3] == 0x00) {
        return FileType::ICO;
    }

    // 14. PDF: 25 50 44 46 ("%PDF")
    if (bytesRead >= 4 &&
        bytes[0] == 0x25 && bytes[1] == 0x50 && bytes[2] == 0x44 && bytes[3] == 0x46) {
        return FileType::PDF;
    }

    // 15. BMP: 42 4D ("BM")
    if (bytesRead >= 2 &&
        bytes[0] == 0x42 && bytes[1] == 0x4D) {
        return FileType::BMP;
    }

    // 16. Conteneurs ZIP (DOCX / ODT): 50 4B 03 04 (ou 50 4B 05 06 / 50 4B 07 08)
    if (bytesRead >= 4 &&
        bytes[0] == 0x50 && bytes[1] == 0x4B &&
        ((bytes[2] == 0x03 && bytes[3] == 0x04) ||
         (bytes[2] == 0x05 && bytes[3] == 0x06) ||
         (bytes[2] == 0x07 && bytes[3] == 0x08))) {
        if (ext == ".docx") {
            return FileType::DOCX;
        }
        if (ext == ".odt") {
            return FileType::ODT;
        }
    }

    // 17. SVG (recherche de "<svg" dans les 512 premiers octets)
    if (headerAscii.find("<svg") != std::string::npos) {
        return FileType::SVG;
    }

    // 18. Documents texte / balisés (HTML, MD, TXT)
    if (ext == ".html" || ext == ".htm" ||
        headerAscii.find("<!doctype html") != std::string::npos ||
        headerAscii.find("<html") != std::string::npos) {
        return FileType::HTML;
    }
    if (ext == ".md" || ext == ".markdown") {
        return FileType::MD;
    }
    if (ext == ".txt") {
        return FileType::TXT;
    }
    if (ext == ".docx") {
        return FileType::DOCX;
    }
    if (ext == ".odt") {
        return FileType::ODT;
    }

    // 19. Fallback par extension pour RAW et conteneurs vidéo à métadonnées variables
    if (isRawExtension(ext)) {
        return FileType::RAW;
    }
    if (ext == ".mp4") {
        return FileType::MP4;
    }
    if (ext == ".mkv") {
        return FileType::MKV;
    }
    if (ext == ".avi") {
        return FileType::AVI;
    }
    if (ext == ".mov") {
        return FileType::MOV;
    }
    if (ext == ".webm") {
        return FileType::WEBM;
    }

    return FileType::UNKNOWN;
}

FileCategory FileDetector::getCategory(FileType type) {
    switch (type) {
        case FileType::PNG:
        case FileType::JPEG:
        case FileType::BMP:
        case FileType::GIF:
        case FileType::WEBP:
        case FileType::ICO:
        case FileType::TIFF:
        case FileType::RAW:
        case FileType::HEIC:
        case FileType::SVG:
            return FileCategory::IMAGE;

        case FileType::WAV:
        case FileType::MP3:
        case FileType::OGG:
        case FileType::FLAC:
        case FileType::AAC:
        case FileType::M4A:
            return FileCategory::AUDIO;

        case FileType::MP4:
        case FileType::MKV:
        case FileType::AVI:
        case FileType::MOV:
        case FileType::WEBM:
            return FileCategory::VIDEO;

        case FileType::PDF:
        case FileType::DOCX:
        case FileType::ODT:
        case FileType::MD:
        case FileType::HTML:
        case FileType::TXT:
            return FileCategory::DOCUMENT;

        case FileType::UNKNOWN:
        default:
            return FileCategory::UNKNOWN;
    }
}

std::string FileDetector::categoryToString(FileCategory category) {
    switch (category) {
        case FileCategory::IMAGE:
            return "Image";
        case FileCategory::AUDIO:
            return "Audio";
        case FileCategory::VIDEO:
            return "Vidéo";
        case FileCategory::DOCUMENT:
            return "Document";
        case FileCategory::UNKNOWN:
        default:
            return "Inconnu";
    }
}

std::string FileDetector::toString(FileType type) {
    switch (type) {
        case FileType::PNG:
            return "PNG";
        case FileType::JPEG:
            return "JPEG";
        case FileType::BMP:
            return "BMP";
        case FileType::GIF:
            return "GIF";
        case FileType::WEBP:
            return "WEBP";
        case FileType::ICO:
            return "ICO";
        case FileType::TIFF:
            return "TIFF";
        case FileType::RAW:
            return "RAW";
        case FileType::HEIC:
            return "HEIC";
        case FileType::SVG:
            return "SVG";
        case FileType::WAV:
            return "WAV";
        case FileType::MP3:
            return "MP3";
        case FileType::OGG:
            return "OGG";
        case FileType::FLAC:
            return "FLAC";
        case FileType::AAC:
            return "AAC";
        case FileType::M4A:
            return "M4A";
        case FileType::MP4:
            return "MP4";
        case FileType::MKV:
            return "MKV";
        case FileType::AVI:
            return "AVI";
        case FileType::MOV:
            return "MOV";
        case FileType::WEBM:
            return "WEBM";
        case FileType::PDF:
            return "PDF";
        case FileType::DOCX:
            return "DOCX";
        case FileType::ODT:
            return "ODT";
        case FileType::MD:
            return "MD";
        case FileType::HTML:
            return "HTML";
        case FileType::TXT:
            return "TXT";
        case FileType::UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

std::string FileDetector::toExtension(FileType type) {
    switch (type) {
        case FileType::PNG:
            return ".png";
        case FileType::JPEG:
            return ".jpg";
        case FileType::BMP:
            return ".bmp";
        case FileType::GIF:
            return ".gif";
        case FileType::WEBP:
            return ".webp";
        case FileType::ICO:
            return ".ico";
        case FileType::TIFF:
            return ".tiff";
        case FileType::HEIC:
            return ".heic";
        case FileType::SVG:
            return ".svg";
        case FileType::WAV:
            return ".wav";
        case FileType::MP3:
            return ".mp3";
        case FileType::OGG:
            return ".ogg";
        case FileType::FLAC:
            return ".flac";
        case FileType::AAC:
            return ".aac";
        case FileType::M4A:
            return ".m4a";
        case FileType::MP4:
            return ".mp4";
        case FileType::MKV:
            return ".mkv";
        case FileType::AVI:
            return ".avi";
        case FileType::MOV:
            return ".mov";
        case FileType::WEBM:
            return ".webm";
        case FileType::PDF:
            return ".pdf";
        case FileType::DOCX:
            return ".docx";
        case FileType::ODT:
            return ".odt";
        case FileType::MD:
            return ".md";
        case FileType::HTML:
            return ".html";
        case FileType::TXT:
            return ".txt";
        case FileType::RAW:
        case FileType::UNKNOWN:
        default:
            return "";
    }
}
