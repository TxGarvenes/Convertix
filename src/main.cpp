#include "Converter.hpp"
#include "FileDetector.hpp"
#include "FormatMatrix.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

std::optional<FileType> parseTargetFormat(std::string arg) {
    std::transform(arg.begin(), arg.end(), arg.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (!arg.empty() && arg.front() == '.') {
        arg.erase(0, 1);
    }

    if (arg == "PNG") return FileType::PNG;
    if (arg == "JPEG" || arg == "JPG") return FileType::JPEG;
    if (arg == "WEBP") return FileType::WEBP;
    if (arg == "BMP") return FileType::BMP;
    if (arg == "GIF") return FileType::GIF;
    return std::nullopt;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <chemin_du_fichier> [format_cible]\n";
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

    if (detectedType == FileType::UNKNOWN) {
        std::cerr << "Erreur : Format non supporte ou fichier introuvable.\n";
        return 1;
    }

    const auto targets = FormatMatrix::getAvailableTargets(detectedType);
    std::cout << "Formats cibles disponibles : ";
    for (std::size_t i = 0; i < targets.size(); ++i) {
        std::cout << FileDetector::toString(targets[i]);
        if (i + 1 < targets.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "\n";

    if (argc < 3) {
        return 0;
    }

    const auto requestedTarget = parseTargetFormat(argv[2]);
    if (!requestedTarget.has_value()) {
        std::cerr << "Erreur : Format cible '" << argv[2] << "' inconnu.\n";
        return 1;
    }

    const bool isAllowed = std::find(targets.begin(), targets.end(), *requestedTarget) != targets.end();
    if (!isAllowed) {
        std::cerr << "Erreur : La conversion de " << FileDetector::toString(detectedType)
                  << " vers " << FileDetector::toString(*requestedTarget)
                  << " n'est pas autorisee.\n";
        return 1;
    }

    Converter converter;
    if (!converter.isMagickAvailable()) {
        std::cerr << "Erreur : ImageMagick ('magick') n'est pas disponible dans le PATH.\n";
        return 1;
    }

    std::cout << "Lancement de la conversion vers " << FileDetector::toString(*requestedTarget) << "...\n";
    converter.startConversion(filePath, *requestedTarget);

    while (converter.getStatus() == ConversionStatus::IN_PROGRESS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (converter.getStatus() == ConversionStatus::SUCCESS) {
        const auto outPath = converter.getLastOutputPath();
        const FileType outDetected = FileDetector::detect(outPath);
        std::cout << "Succes ! Fichier cree : " << outPath.string()
                  << " (verifie via Magic Bytes : " << FileDetector::toString(outDetected) << ")\n";
        return 0;
    } else {
        std::cerr << "Erreur : La conversion via ImageMagick a echoue.\n";
        return 1;
    }
}
