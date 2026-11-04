#include "Converter.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

Converter::~Converter() {
    if (m_task.valid()) {
        m_task.wait();
    }
}

bool Converter::isMagickAvailable() const {
    if (const char* pathEnv = std::getenv("PATH")) {
        std::stringstream ss(pathEnv);
        std::string dir;
#ifdef _WIN32
        const char delimiter = ';';
        const std::string exeName = "magick.exe";
#else
        const char delimiter = ':';
        const std::string exeName = "magick";
#endif
        std::error_code ec;
        while (std::getline(ss, dir, delimiter)) {
            if (!dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / exeName, ec)) {
                return true;
            }
        }
    }

#ifdef _WIN32
    const int ret = std::system("magick -version > NUL 2>&1");
#else
    const int ret = std::system("magick -version > /dev/null 2>&1");
#endif
    return ret == 0;
}

void Converter::startConversion(const std::filesystem::path& input, FileType targetType) {
    if (m_status.load() == ConversionStatus::IN_PROGRESS) {
        return;
    }

    if (m_task.valid()) {
        m_task.wait();
    }

    const std::string ext = FileDetector::toExtension(targetType);
    if (ext.empty()) {
        m_status.store(ConversionStatus::ERROR_CLI);
        return;
    }

    const std::string outputFileName = input.stem().string() + "_converted" + ext;
    const std::filesystem::path outputPath = input.parent_path() / outputFileName;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastOutputPath = outputPath;
    }

    m_status.store(ConversionStatus::IN_PROGRESS);

    m_task = std::async(std::launch::async, [this, input, outputPath, targetType, ext]() {
        std::string options;
        if (targetType == FileType::JPEG || ext == ".jpg" || ext == ".jpeg") {
            // Suppression de la transparence uniquement pour JPEG (conservation de l'alpha pour PNG, WEBP, TIFF, ICO)
            options = " -background white -alpha remove -alpha off";
        } else if (targetType == FileType::ICO || ext == ".ico") {
            // Redimensionnement automatique multi-résolution pour ICO (max 256x256) tout en conservant le canal alpha
            options = " -define icon:auto-resize=256,128,64,48,32,16";
        }

        const std::string command =
            "magick \"" + input.string() + "\"" + options + " \"" + outputPath.string() + "\"";
        const int result = std::system(command.c_str());

        std::error_code ec;
        if (result == 0 && std::filesystem::exists(outputPath, ec)) {
            m_status.store(ConversionStatus::SUCCESS);
        } else {
            m_status.store(ConversionStatus::ERROR_CLI);
        }
    });
}

ConversionStatus Converter::getStatus() const {
    return m_status.load();
}

std::filesystem::path Converter::getLastOutputPath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastOutputPath;
}

void Converter::reset() {
    if (m_task.valid()) {
        m_task.wait();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastOutputPath.clear();
    }
    m_status.store(ConversionStatus::IDLE);
}
