#pragma once

#include "FileDetector.hpp"

#include <vector>

class FormatMatrix {
public:
    static std::vector<FileType> getAvailableTargets(FileType sourceType);
};
