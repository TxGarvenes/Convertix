#pragma once

#include "Converter.hpp"
#include "FileDetector.hpp"

#include <cstddef>
#include <filesystem>
#include <future>
#include <optional>
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
    ~App();

    static void initBasePath(const char* argv0 = nullptr);
    static const std::filesystem::path& getBasePath();

    void run();

private:
    static constexpr int WINDOW_WIDTH = 600;
    static constexpr int WINDOW_HEIGHT = 400;
    static constexpr int BASE_FONT_SIZE = 22;

    AppState m_state{AppState::WAITING_DROP};
    std::filesystem::path m_currentFile;
    FileType m_detectedType{FileType::UNKNOWN};
    FileCategory m_detectedCategory{FileCategory::UNKNOWN};
    std::vector<FileType> m_availableTargets;
    std::size_t m_selectedTargetIndex{0};
    bool m_dropdownOpen{false};
    bool m_showSvgWarningModal{false};
    int m_imageWidth{0};
    int m_imageHeight{0};

    float m_pathScrollOffset{0.0f};
    std::string m_outputSizeFormatted;

    Converter m_converter;

    std::future<std::optional<std::filesystem::path>> m_fileDialogTask;
    bool m_fileDialogOpen{false};

    Font m_font{};
    bool m_customFontLoaded{false};

    Texture2D m_logoTexture{};
    bool m_logoLoaded{false};

    void loadAppFont();
    void unloadAppFont();
    void loadAppIcon();
    void loadAppLogo();
    void unloadAppLogo();

    void selectFile(const std::filesystem::path& filePath);
    void openFileDialogAsync();
    static void openOutputFolderAsync(const std::filesystem::path& outputPath);
    void handleDroppedFiles();
    void update();
    void render();
    void resetState();
    void returnToFormatSelection();

    void drawCenteredText(const std::string& text, float y, float fontSize, Color color) const;
    bool drawButton(Rectangle bounds, const std::string& label, Color baseColor, Color hoverColor, Color textColor, bool enabled = true) const;
    std::string fitTextToWidth(const std::string& text, float fontSize, float maxWidth, bool keepEnd = false) const;
    static std::string formatFileSize(const std::filesystem::path& filePath);
};
