#pragma once

#include "FileDetector.hpp"

#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>

enum class ConversionStatus {
    IDLE,
    IN_PROGRESS,
    SUCCESS,
    ERROR_CLI
};

class Converter {
public:
    Converter() = default;
    ~Converter();

    Converter(const Converter&) = delete;
    Converter& operator=(const Converter&) = delete;

    bool isMagickAvailable() const;
    void startConversion(const std::filesystem::path& input, FileType targetType);
    ConversionStatus getStatus() const;
    std::filesystem::path getLastOutputPath() const;
    void reset();

private:
    std::atomic<ConversionStatus> m_status{ConversionStatus::IDLE};
    mutable std::mutex m_mutex;
    std::filesystem::path m_lastOutputPath;
    std::future<void> m_task;
};
