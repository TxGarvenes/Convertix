#include "Converter.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

Converter::Converter() {
    checkToolsAvailability();
}

Converter::~Converter() {
    if (m_task.valid()) {
        m_task.wait();
    }
}

bool Converter::isExecutableInPath(const std::string& toolName) {
    if (const char* pathEnv = std::getenv("PATH")) {
        std::stringstream ss(pathEnv);
        std::string dir;
#ifdef _WIN32
        const char delimiter = ';';
        const std::string exeName = toolName + ".exe";
#else
        const char delimiter = ':';
        const std::string exeName = toolName;
#endif
        std::error_code ec;
        while (std::getline(ss, dir, delimiter)) {
            if (!dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / exeName, ec)) {
                return true;
            }
        }
    }
    return false;
}

bool Converter::checkToolsAvailability() {
    m_hasMagick = isExecutableInPath("magick");
    m_hasFfmpeg = isExecutableInPath("ffmpeg");
    m_hasPandoc = isExecutableInPath("pandoc");
    return m_hasMagick || m_hasFfmpeg || m_hasPandoc;
}

bool Converter::isMagickAvailable() const {
    return m_hasMagick;
}

bool Converter::isFfmpegAvailable() const {
    return m_hasFfmpeg;
}

bool Converter::isPandocAvailable() const {
    return m_hasPandoc;
}

void Converter::startConversion(const std::filesystem::path& input, FileType targetType) {
    startConversion(input, FileDetector::detect(input), targetType);
}

void Converter::startConversion(const std::filesystem::path& input, FileType sourceType, FileType targetType) {
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

    const FileCategory sourceCat = FileDetector::getCategory(sourceType);
    const FileCategory targetCat = FileDetector::getCategory(targetType);

    // Détermination de l'outil CLI requis selon la catégorie source/cible
    const bool useFfmpeg = (sourceCat == FileCategory::AUDIO || sourceCat == FileCategory::VIDEO ||
                            targetCat == FileCategory::AUDIO || targetCat == FileCategory::VIDEO);
    const bool usePandoc = (!useFfmpeg && sourceCat == FileCategory::DOCUMENT &&
                            targetCat == FileCategory::DOCUMENT);
    const bool useMagick = (!useFfmpeg && !usePandoc);

    if ((useFfmpeg && !m_hasFfmpeg) || (useMagick && !m_hasMagick) || (usePandoc && !m_hasPandoc)) {
        m_status.store(ConversionStatus::MISSING_TOOL);
        return;
    }

    const std::string outputFileName = input.stem().string() + "_converted" + ext;
    const std::filesystem::path outputPath = input.parent_path() / outputFileName;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastOutputPath = outputPath;
    }

    m_status.store(ConversionStatus::IN_PROGRESS);

    m_task = std::async(std::launch::async, [this, input, outputPath, sourceCat, targetCat, targetType, ext, useFfmpeg, usePandoc]() {
        std::string command;

#ifdef _WIN32
        const std::string silentRedirect = " > NUL 2>&1";
#else
        const std::string silentRedirect = " > /dev/null 2>&1";
#endif

        if (useFfmpeg) {
            std::string extraFlags;
            if (sourceCat == FileCategory::VIDEO && targetCat == FileCategory::AUDIO) {
                extraFlags = " -vn";
            }
            command = "ffmpeg -y -i \"" + input.string() + "\"" + extraFlags +
                      " \"" + outputPath.string() + "\"" + silentRedirect;
        } else if (usePandoc) {
            command = "pandoc \"" + input.string() + "\" -o \"" + outputPath.string() + "\"" + silentRedirect;
        } else {
            std::string options;
            if (targetType == FileType::JPEG || ext == ".jpg" || ext == ".jpeg") {
                options = " -background white -alpha remove -alpha off";
            } else if (targetType == FileType::ICO || ext == ".ico") {
                options = " -define icon:auto-resize=256,128,64,48,32,16";
            }
            command = "magick \"" + input.string() + "\"" + options +
                      " \"" + outputPath.string() + "\"" + silentRedirect;
        }

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
