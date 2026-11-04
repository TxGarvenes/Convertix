#pragma once

#include <filesystem>
#include <string>

enum class FileType {
    PNG,
    JPEG,
    BMP,
    GIF,
    WEBP,
    UNKNOWN
};

class FileDetector {
public:
    static FileType detect(const std::filesystem::path& filePath);
    static std::string toString(FileType type);
    static std::string toExtension(FileType type);
};
