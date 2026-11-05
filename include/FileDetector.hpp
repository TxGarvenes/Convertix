#pragma once

#include <filesystem>
#include <string>

enum class FileCategory {
    IMAGE,
    AUDIO,
    VIDEO,
    DOCUMENT,
    UNKNOWN
};

enum class FileType {
    // Images
    PNG,
    JPEG,
    BMP,
    GIF,
    WEBP,
    ICO,
    TIFF,
    RAW,
    HEIC,
    SVG,

    // Audio
    WAV,
    MP3,
    OGG,
    FLAC,
    AAC,
    M4A,

    // Vidéo
    MP4,
    MKV,
    AVI,
    MOV,
    WEBM,

    // Documents
    PDF,
    DOCX,
    ODT,
    MD,
    HTML,
    TXT,

    UNKNOWN
};

class FileDetector {
public:
    static FileType detect(const std::filesystem::path& filePath);
    static FileCategory getCategory(FileType type);
    static std::string categoryToString(FileCategory category);
    static std::string toString(FileType type);
    static std::string toExtension(FileType type);
};
