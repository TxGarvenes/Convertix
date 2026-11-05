#include "FormatMatrix.hpp"

#include <algorithm>

namespace {

std::vector<FileType> filterOutSource(std::vector<FileType> candidates, FileType sourceType) {
    candidates.erase(
        std::remove(candidates.begin(), candidates.end(), sourceType),
        candidates.end()
    );
    return candidates;
}

} // namespace

std::vector<FileType> FormatMatrix::getAvailableTargets(FileType sourceType) {
    switch (sourceType) {
        // Images courantes -> PNG, JPEG, WEBP, BMP, TIFF, ICO, PDF (moins le format source)
        case FileType::PNG:
        case FileType::JPEG:
        case FileType::WEBP:
        case FileType::BMP:
        case FileType::TIFF:
            return filterOutSource(
                {FileType::PNG, FileType::JPEG, FileType::WEBP, FileType::BMP, FileType::TIFF, FileType::ICO, FileType::PDF},
                sourceType
            );

        // Images spéciales en entrée seule -> PNG, JPEG, WEBP, TIFF, PDF
        case FileType::RAW:
        case FileType::HEIC:
        case FileType::SVG:
            return {FileType::PNG, FileType::JPEG, FileType::WEBP, FileType::TIFF, FileType::PDF};

        // Image animée GIF -> WEBP, PNG (cloisonnement strict : jamais de cible vidéo depuis une image)
        case FileType::GIF:
            return {FileType::WEBP, FileType::PNG};

        // Icône ICO -> PNG, JPEG, WEBP, BMP
        case FileType::ICO:
            return {FileType::PNG, FileType::JPEG, FileType::WEBP, FileType::BMP};

        // Audio -> MP3, WAV, FLAC, OGG, AAC (moins le format source, jamais de cible vidéo)
        case FileType::MP3:
        case FileType::WAV:
        case FileType::FLAC:
        case FileType::OGG:
        case FileType::AAC:
        case FileType::M4A:
            return filterOutSource(
                {FileType::MP3, FileType::WAV, FileType::FLAC, FileType::OGG, FileType::AAC},
                sourceType
            );

        // Vidéo -> Vidéo (MP4, MKV, AVI, WEBM moins le format source) + Extraction Audio (MP3, WAV, OGG, FLAC)
        case FileType::MP4:
        case FileType::MKV:
        case FileType::AVI:
        case FileType::MOV:
        case FileType::WEBM:
            return filterOutSource(
                {
                    FileType::MP4, FileType::MKV, FileType::AVI, FileType::WEBM,
                    FileType::MP3, FileType::WAV, FileType::OGG, FileType::FLAC
                },
                sourceType
            );

        // Documents -> DOCX, ODT, MD, HTML, TXT (moins le format source, cloisonnement strict)
        case FileType::DOCX:
        case FileType::ODT:
        case FileType::MD:
        case FileType::HTML:
        case FileType::TXT:
            return filterOutSource(
                {FileType::DOCX, FileType::ODT, FileType::MD, FileType::HTML, FileType::TXT},
                sourceType
            );

        case FileType::UNKNOWN:
        default:
            return {};
    }
}
