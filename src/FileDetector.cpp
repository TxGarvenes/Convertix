#include "FileDetector.hpp"

#include <array>
#include <cstdint>
#include <fstream>

FileType FileDetector::detect(const std::filesystem::path& filePath) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec) || !std::filesystem::is_regular_file(filePath, ec)) {
        return FileType::UNKNOWN;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return FileType::UNKNOWN;
    }

    std::array<std::uint8_t, 12> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto bytesRead = file.gcount();

    if (bytesRead < 2) {
        return FileType::UNKNOWN;
    }

    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (bytesRead >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
        return FileType::PNG;
    }

    // JPEG: FF D8 FF
    if (bytesRead >= 3 &&
        bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return FileType::JPEG;
    }

    // WEBP: 52 49 46 46 (RIFF) at 0..3 && 57 45 42 50 (WEBP) at 8..11
    if (bytesRead >= 12 &&
        bytes[0] == 0x52 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x46 &&
        bytes[8] == 0x57 && bytes[9] == 0x45 && bytes[10] == 0x42 && bytes[11] == 0x50) {
        return FileType::WEBP;
    }

    // GIF: 47 49 46 38 37 61 (GIF87a) || 47 49 46 38 39 61 (GIF89a)
    if (bytesRead >= 6 &&
        bytes[0] == 0x47 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x38 &&
        (bytes[4] == 0x37 || bytes[4] == 0x39) && bytes[5] == 0x61) {
        return FileType::GIF;
    }

    // BMP: 42 4D
    if (bytesRead >= 2 &&
        bytes[0] == 0x42 && bytes[1] == 0x4D) {
        return FileType::BMP;
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
        case FileType::UNKNOWN:
        default:
            return "";
    }
}
