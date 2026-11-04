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

    std::array<std::uint8_t, 16> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto bytesRead = file.gcount();

    if (bytesRead < 2) {
        return FileType::UNKNOWN;
    }

    // PNG: 89 50 4E 47 0D 0A 1A 0A (8 bytes)
    if (bytesRead >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
        return FileType::PNG;
    }

    // JPEG: FF D8 FF (3 bytes)
    if (bytesRead >= 3 &&
        bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return FileType::JPEG;
    }

    // WEBP: 52 49 46 46 ("RIFF") at 0..3 AND 57 45 42 50 ("WEBP") at 8..11
    if (bytesRead >= 12 &&
        bytes[0] == 0x52 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x46 &&
        bytes[8] == 0x57 && bytes[9] == 0x45 && bytes[10] == 0x42 && bytes[11] == 0x50) {
        return FileType::WEBP;
    }

    // HEIC / HEIF: "ftyp" at offset 4..7 and brand ("heic", "heix", "mif1", "msf1") at offset 8..11
    if (bytesRead >= 12 &&
        bytes[4] == 'f' && bytes[5] == 't' && bytes[6] == 'y' && bytes[7] == 'p') {
        const std::string brand(reinterpret_cast<const char*>(&bytes[8]), 4);
        if (brand == "heic" || brand == "heix" || brand == "mif1" || brand == "msf1" ||
            brand == "hevc" || brand == "hevx") {
            return FileType::HEIC;
        }
    }

    // GIF: 47 49 46 38 37 61 (GIF87a) or 47 49 46 38 39 61 (GIF89a) (6 bytes)
    if (bytesRead >= 6 &&
        bytes[0] == 0x47 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x38 &&
        (bytes[4] == 0x37 || bytes[4] == 0x39) && bytes[5] == 0x61) {
        return FileType::GIF;
    }

    // TIFF / DNG / RAW générique: 49 49 2A 00 (Little-Endian) ou 4D 4D 00 2A (Big-Endian)
    if (bytesRead >= 4 &&
        ((bytes[0] == 0x49 && bytes[1] == 0x49 && bytes[2] == 0x2A && bytes[3] == 0x00) ||
         (bytes[0] == 0x4D && bytes[1] == 0x4D && bytes[2] == 0x00 && bytes[3] == 0x2A))) {
        // Signature spécifique Canon CR2 ("CR" à l'offset 8..9) ou fallback par extension RAW (.dng, .cr2, .nef, .arw)
        if ((bytesRead >= 10 && bytes[8] == 0x43 && bytes[9] == 0x52) ||
            isRawExtension(getLowerExtension(filePath))) {
            return FileType::RAW;
        }
        return FileType::TIFF;
    }

    // ICO: 00 00 01 00 (4 bytes)
    if (bytesRead >= 4 &&
        bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x01 && bytes[3] == 0x00) {
        return FileType::ICO;
    }

    // BMP: 42 4D (BM) (2 bytes)
    if (bytesRead >= 2 &&
        bytes[0] == 0x42 && bytes[1] == 0x4D) {
        return FileType::BMP;
    }

    // Fallback par extension pour les formats photo RAW spécifiques (CR2, NEF, ARW, DNG)
    if (isRawExtension(getLowerExtension(filePath))) {
        return FileType::RAW;
    }

    return FileType::UNKNOWN;
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
        case FileType::HEIC:
            return "HEIC";
        case FileType::TIFF:
            return "TIFF";
        case FileType::ICO:
            return "ICO";
        case FileType::RAW:
            return "RAW";
        case FileType::PDF:
            return "PDF";
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
        case FileType::HEIC:
            return ".heic";
        case FileType::TIFF:
            return ".tiff";
        case FileType::ICO:
            return ".ico";
        case FileType::PDF:
            return ".pdf";
        case FileType::RAW:
        case FileType::UNKNOWN:
        default:
            return "";
    }
}
