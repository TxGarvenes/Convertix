#include "Converter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {

std::string getToolSubdir(const std::string& toolName) {
    if (toolName == "magick") {
        return "imagemagick";
    }
    if (toolName == "ffprobe") {
        return "ffmpeg";
    }
    return toolName;
}

FILE* openProcessPipe(const std::string& command) {
#if defined(_WIN32)
    std::string winCmd = command;
    if (!winCmd.empty() && winCmd.front() == '"') {
        winCmd = "\"" + winCmd + "\"";
    }
    return _popen(winCmd.c_str(), "r");
#else
    return popen(command.c_str(), "r");
#endif
}

int closeProcessPipe(FILE* pipe) {
#if defined(_WIN32)
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

// Parse une chaîne "HH:MM:SS.xx" en secondes
double parseHMS(const std::string& hms) {
    int h = 0;
    int m = 0;
    double s = 0.0;
    if (std::sscanf(hms.c_str(), "%d:%d:%lf", &h, &m, &s) == 3) {
        return static_cast<double>(h) * 3600.0 + static_cast<double>(m) * 60.0 + s;
    }
    return 0.0;
}

} // namespace

Converter::Converter() {
    checkToolsAvailability();
}

Converter::~Converter() {
    if (m_task.valid()) {
        m_task.wait();
    }
}

std::string Converter::resolveToolPath(const std::string& toolName) {
    const std::string subDir = getToolSubdir(toolName);
    std::error_code ec;

#if defined(_WIN32)
    const std::string exeFile = toolName + ".exe";
    const std::array<std::filesystem::path, 4> localCandidates = {
        std::filesystem::path("bin") / subDir / exeFile,
        std::filesystem::path("bin") / exeFile,
        std::filesystem::path("../bin") / subDir / exeFile,
        std::filesystem::path("../bin") / exeFile
    };

    for (const auto& candidate : localCandidates) {
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
            const auto absPath = std::filesystem::weakly_canonical(candidate, ec);
            const std::string resolved = ec ? candidate.string() : absPath.string();
            return "\"" + resolved + "\"";
        }
    }
#else
    const std::array<std::filesystem::path, 4> localCandidates = {
        std::filesystem::path("bin") / subDir / toolName,
        std::filesystem::path("bin") / toolName,
        std::filesystem::path("../bin") / subDir / toolName,
        std::filesystem::path("../bin") / toolName
    };

    for (const auto& candidate : localCandidates) {
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
            const auto absPath = std::filesystem::weakly_canonical(candidate, ec);
            const std::string resolved = ec ? candidate.string() : absPath.string();
            return "\"" + resolved + "\"";
        }
    }
#endif

    return toolName;
}

bool Converter::isToolAvailable(const std::string& toolName) {
    const std::string resolved = resolveToolPath(toolName);
    if (!resolved.empty() && resolved.front() == '"') {
        return true;
    }

    if (const char* pathEnv = std::getenv("PATH")) {
        std::stringstream ss(pathEnv);
        std::string dir;
#if defined(_WIN32)
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

double Converter::probeMediaDurationSeconds(const std::filesystem::path& input) {
    if (!isToolAvailable("ffprobe")) {
        return 0.0;
    }

    const std::string ffprobeCmd = resolveToolPath("ffprobe") +
        " -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" +
        input.string() + "\" 2>&1";

    FILE* pipe = openProcessPipe(ffprobeCmd);
    if (!pipe) {
        return 0.0;
    }

    std::array<char, 256> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    closeProcessPipe(pipe);

    try {
        const double sec = std::stod(output);
        return (sec > 0.0) ? sec : 0.0;
    } catch (...) {
        return 0.0;
    }
}

bool Converter::checkToolsAvailability() {
    m_hasMagick = isToolAvailable("magick");
    m_hasFfmpeg = isToolAvailable("ffmpeg");
    m_hasPandoc = isToolAvailable("pandoc");
    return m_hasMagick || m_hasFfmpeg || m_hasPandoc;
}

bool Converter::isMagickAvailable() const {
    return m_hasMagick;
}

bool Converter::isFFmpegAvailable() const {
    return m_hasFfmpeg;
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

    m_progress.store(0.0f);

    const std::string ext = FileDetector::toExtension(targetType);
    if (ext.empty()) {
        m_status.store(ConversionStatus::ERROR_CLI);
        return;
    }

    const FileCategory sourceCat = FileDetector::getCategory(sourceType);
    const FileCategory targetCat = FileDetector::getCategory(targetType);

    const bool useFfmpeg = (sourceCat == FileCategory::AUDIO || sourceCat == FileCategory::VIDEO ||
                            targetCat == FileCategory::AUDIO || targetCat == FileCategory::VIDEO);
    const bool usePandoc = (!useFfmpeg && sourceCat == FileCategory::DOCUMENT &&
                            targetCat == FileCategory::DOCUMENT);
    const bool useMagick = (!useFfmpeg && !usePandoc);

    if ((useFfmpeg && !isFFmpegAvailable()) ||
        (useMagick && !isMagickAvailable()) ||
        (usePandoc && !isPandocAvailable())) {
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
        if (useFfmpeg) {
            double totalDurationSec = probeMediaDurationSeconds(input);
            const std::string ffmpegCmd = resolveToolPath("ffmpeg");
            std::string encodeArgs;

            if (sourceCat == FileCategory::VIDEO && targetCat == FileCategory::VIDEO) {
                if (targetType == FileType::WEBM || targetType == FileType::AVI) {
                    encodeArgs = "";
                } else {
                    encodeArgs = " -c:v libx264 -crf 23 -preset medium -c:a aac -b:a 192k";
                }
            } else if (sourceCat == FileCategory::VIDEO && targetCat == FileCategory::AUDIO) {
                encodeArgs = " -vn -q:a 2";
            }

            const std::string command =
                ffmpegCmd + " -y -i \"" + input.string() + "\"" + encodeArgs +
                " -stats_period 0.05 -progress pipe:1 -nostats \"" + outputPath.string() + "\" 2>&1";

            FILE* pipe = openProcessPipe(command);
            int exitCode = -1;

            if (pipe) {
                std::array<char, 512> lineBuf{};
                while (std::fgets(lineBuf.data(), static_cast<int>(lineBuf.size()), pipe) != nullptr) {
                    const std::string line(lineBuf.data());

                    // Fallback d'extraction de durée depuis l'en-tête stderr de ffmpeg si ffprobe n'était pas disponible
                    if (totalDurationSec <= 0.0) {
                        const auto durPos = line.find("Duration:");
                        if (durPos != std::string::npos) {
                            const std::string durSub = line.substr(durPos + 9);
                            const auto commaPos = durSub.find(',');
                            if (commaPos != std::string::npos) {
                                std::string hms = durSub.substr(0, commaPos);
                                hms.erase(0, hms.find_first_not_of(" \t"));
                                totalDurationSec = parseHMS(hms);
                            }
                        }
                    }

                    if (totalDurationSec > 0.0) {
                        if (line.rfind("out_time_us=", 0) == 0 || line.rfind("out_time_ms=", 0) == 0) {
                            try {
                                const long long currentUs = std::stoll(line.substr(12));
                                if (currentUs > 0) {
                                    const double currentSec = static_cast<double>(currentUs) / 1000000.0;
                                    const float pct = static_cast<float>((currentSec / totalDurationSec) * 100.0);
                                    m_progress.store(std::clamp(pct, 0.0f, 99.0f));
                                }
                            } catch (...) {}
                        } else if (line.rfind("out_time=", 0) == 0) {
                            const double currentSec = parseHMS(line.substr(9));
                            if (currentSec > 0.0) {
                                const float pct = static_cast<float>((currentSec / totalDurationSec) * 100.0);
                                m_progress.store(std::clamp(pct, 0.0f, 99.0f));
                            }
                        }
                    }

                    if (line.rfind("progress=end", 0) == 0) {
                        m_progress.store(100.0f);
                    }
                }
                exitCode = closeProcessPipe(pipe);
            }

            std::error_code ec;
            if (exitCode == 0 && std::filesystem::exists(outputPath, ec)) {
                m_progress.store(100.0f);
                m_status.store(ConversionStatus::SUCCESS);
            } else {
                m_status.store(ConversionStatus::ERROR_CLI);
            }
            return;
        }

#if defined(_WIN32)
        const std::string silentRedirect = " > NUL 2>&1";
#else
        const std::string silentRedirect = " > /dev/null 2>&1";
#endif

        std::string command;
        if (usePandoc) {
            const std::string pandocCmd = resolveToolPath("pandoc");
            command = pandocCmd + " -s \"" + input.string() + "\" -o \"" +
                      outputPath.string() + "\"" + silentRedirect;
        } else {
            const std::string magickCmd = resolveToolPath("magick");
            std::string options;
            if (targetType == FileType::JPEG || ext == ".jpg" || ext == ".jpeg") {
                options = " -background white -alpha remove -alpha off";
            } else if (targetType == FileType::ICO || ext == ".ico") {
                options = " -define icon:auto-resize=256,128,64,48,32,16";
            }
            command = magickCmd + " \"" + input.string() + "\"" + options +
                      " \"" + outputPath.string() + "\"" + silentRedirect;
        }

#if defined(_WIN32)
        if (!command.empty() && command.front() == '"') {
            command = "\"" + command + "\"";
        }
#endif

        const int result = std::system(command.c_str());

        std::error_code ec;
        if (result == 0 && std::filesystem::exists(outputPath, ec)) {
            m_progress.store(100.0f);
            m_status.store(ConversionStatus::SUCCESS);
        } else {
            m_status.store(ConversionStatus::ERROR_CLI);
        }
    });
}

ConversionStatus Converter::getStatus() const {
    return m_status.load();
}

float Converter::getProgress() const {
    return m_progress.load();
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
    m_progress.store(0.0f);
    m_status.store(ConversionStatus::IDLE);
}
