#include "FileDetector.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <chemin_du_fichier>\n";
        return 1;
    }

    const std::filesystem::path filePath{argv[1]};
    const FileType detectedType = FileDetector::detect(filePath);

    std::cout << "Fichier : " << filePath.string() << "\n"
              << "Format detecte : " << FileDetector::toString(detectedType);

    const std::string ext = FileDetector::toExtension(detectedType);
    if (!ext.empty()) {
        std::cout << " (" << ext << ")";
    }
    std::cout << "\n";

    return (detectedType == FileType::UNKNOWN) ? 1 : 0;
}
