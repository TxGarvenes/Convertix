#include "FormatMatrix.hpp"

std::vector<FileType> FormatMatrix::getAvailableTargets(FileType sourceType) {
    switch (sourceType) {
        case FileType::PNG:
            return {FileType::JPEG, FileType::WEBP, FileType::BMP, FileType::TIFF, FileType::ICO};
        case FileType::JPEG:
            return {FileType::PNG, FileType::WEBP, FileType::BMP, FileType::TIFF, FileType::ICO};
        case FileType::BMP:
            return {FileType::PNG, FileType::JPEG, FileType::WEBP, FileType::TIFF, FileType::ICO};
        case FileType::WEBP:
            return {FileType::PNG, FileType::JPEG, FileType::BMP, FileType::TIFF, FileType::ICO};
        case FileType::RAW:
        case FileType::HEIC:
        case FileType::TIFF:
            return {FileType::PNG, FileType::JPEG, FileType::WEBP, FileType::PDF};
        case FileType::ICO:
            return {FileType::PNG, FileType::JPEG, FileType::WEBP, FileType::BMP};
        case FileType::GIF:
            return {FileType::WEBP, FileType::PNG};
        case FileType::PDF:
        case FileType::UNKNOWN:
        default:
            return {};
    }
}
