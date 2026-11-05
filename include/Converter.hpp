#pragma once

#include "FileDetector.hpp"

#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>

enum class ConversionStatus {
    IDLE,
    IN_PROGRESS,
    SUCCESS,
    ERROR_CLI,
    MISSING_TOOL
};

class Converter {
public:
    Converter();
    ~Converter();

    Converter(const Converter&) = delete;
    Converter& operator=(const Converter&) = delete;

    static std::string resolveToolPath(const std::string& toolName);

    bool checkToolsAvailability();
    bool isMagickAvailable() const;
    bool isFFmpegAvailable() const;
    bool isFfmpegAvailable() const;
    bool isPandocAvailable() const;
    bool isPotraceAvailable() const;

    void startConversion(const std::filesystem::path& input, FileType sourceType, FileType targetType);
    void startConversion(const std::filesystem::path& input, FileType targetType);

    ConversionStatus getStatus() const;
    float getProgress() const;
    std::filesystem::path getLastOutputPath() const;
    void reset();

private:
    std::atomic<ConversionStatus> m_status{ConversionStatus::IDLE};
    std::atomic<float> m_progress{0.0f};
    mutable std::mutex m_mutex;
    std::filesystem::path m_lastOutputPath;
    std::future<void> m_task;

    bool m_hasMagick{false};
    bool m_hasFfmpeg{false};
    bool m_hasPandoc{false};
    bool m_hasPotrace{false};

    static bool isToolAvailable(const std::string& toolName);
    static double probeMediaDurationSeconds(const std::filesystem::path& input);
};
