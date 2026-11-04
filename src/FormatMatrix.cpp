#include "FormatMatrix.hpp"

std::vector<FileType> FormatMatrix::getAvailableTargets(FileType sourceType) {
    switch (sourceType) {
        case FileType::PNG:
            return {FileType::JPEG, FileType::WEBP, FileType::BMP};
        case FileType::JPEG:
            return {FileType::PNG, FileType::WEBP, FileType::BMP};
        case FileType::WEBP:
            return {FileType::PNG, FileType::JPEG, FileType::BMP};
        case FileType::BMP:
            return {FileType::PNG, FileType::JPEG, FileType::WEBP};
        case FileType::GIF:
            return {FileType::WEBP, FileType::PNG};
        case FileType::UNKNOWN:
        default:
            return {};
    }
}
