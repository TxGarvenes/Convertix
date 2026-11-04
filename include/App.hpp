#pragma once

#include "Converter.hpp"
#include "FileDetector.hpp"

#include <cstddef>
#include <filesystem>
#include <raylib.h>
#include <string>
#include <vector>

enum class AppState {
    WAITING_DROP,
    FILE_DETECTED,
    CONVERTING,
    FINISHED
};

class App {
public:
    App();
    ~App() = default;

    void run();

private:
    static constexpr int WINDOW_WIDTH = 600;
    static constexpr int WINDOW_HEIGHT = 400;
    static constexpr int BASE_FONT_SIZE = 22;

    AppState m_state{AppState::WAITING_DROP};
    std::filesystem::path m_currentFile;
    FileType m_detectedType{FileType::UNKNOWN};
    std::vector<FileType> m_availableTargets;
    std::size_t m_selectedTargetIndex{0};
    bool m_dropdownOpen{false};

    Converter m_converter;
    bool m_magickAvailable{false};

    Font m_font{};
    bool m_customFontLoaded{false};

    void loadAppFont();
    void unloadAppFont();

    void handleDroppedFiles();
    void update();
    void render();
    void resetState();

    void drawCenteredText(const std::string& text, float y, float fontSize, Color color) const;
    bool drawButton(Rectangle bounds, const std::string& label, Color baseColor, Color hoverColor, Color textColor, bool enabled = true) const;
    std::string fitTextToWidth(const std::string& text, float fontSize, float maxWidth, bool keepEnd = false) const;
};
