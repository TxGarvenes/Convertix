#include "App.hpp"
#include "FormatMatrix.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <thread>

namespace {

constexpr float TEXT_SPACING = 1.0f;

struct DropdownRow {
    bool isSeparator{false};
    std::string label;
    std::size_t targetIndex{0};
};

void drawDashedRect(Rectangle rec, float dashLength, float gapLength, float thickness, Color color) {
    for (float x = rec.x; x < rec.x + rec.width; x += dashLength + gapLength) {
        const float segW = (x + dashLength > rec.x + rec.width) ? (rec.x + rec.width - x) : dashLength;
        DrawRectangleRec({x, rec.y, segW, thickness}, color);
        DrawRectangleRec({x, rec.y + rec.height - thickness, segW, thickness}, color);
    }
    for (float y = rec.y; y < rec.y + rec.height; y += dashLength + gapLength) {
        const float segH = (y + dashLength > rec.y + rec.height) ? (rec.y + rec.height - y) : dashLength;
        DrawRectangleRec({rec.x, y, thickness, segH}, color);
        DrawRectangleRec({rec.x + rec.width - thickness, y, thickness, segH}, color);
    }
}

std::optional<std::filesystem::path> findFirstTtfInDir(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return std::nullopt;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        auto ext = entry.path().extension().string();
        for (char& c : ext) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (ext == ".ttf") {
            return entry.path();
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findAssetFile(const std::string& relativeName) {
    const std::array<std::filesystem::path, 2> candidates = {
        std::filesystem::path("assets") / relativeName,
        std::filesystem::path("../assets") / relativeName
    };

    std::error_code ec;
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec)) {
            return path;
        }
    }
    return std::nullopt;
}

Image loadSvgImage(const std::filesystem::path& svgPath, int targetWidth, int targetHeight) {
    std::error_code ec;
    const std::string cacheFileName =
        "." + svgPath.stem().string() + "_" + std::to_string(targetWidth) + "x" + std::to_string(targetHeight) + ".png";
    const std::filesystem::path cachePng = svgPath.parent_path() / cacheFileName;

    if (std::filesystem::exists(cachePng, ec)) {
        const auto svgTime = std::filesystem::last_write_time(svgPath, ec);
        const auto cacheTime = std::filesystem::last_write_time(cachePng, ec);
        if (!ec && cacheTime >= svgTime) {
            Image cachedImg = LoadImage(cachePng.string().c_str());
            if (IsImageValid(cachedImg)) {
                return cachedImg;
            }
        }
    }

    std::string resizeArg;
    if (targetWidth > 0 && targetHeight > 0) {
        resizeArg = " -resize " + std::to_string(targetWidth) + "x" + std::to_string(targetHeight);
    } else if (targetHeight > 0) {
        resizeArg = " -resize x" + std::to_string(targetHeight);
    }

    const std::string magickCmd = Converter::resolveToolPath("magick");
#ifdef _WIN32
    std::string cmd = magickCmd + " -background none \"" + svgPath.string() + "\"" +
                      resizeArg + " PNG32:\"" + cachePng.string() + "\" > NUL 2>&1";
    if (!cmd.empty() && cmd.front() == '"') {
        cmd = "\"" + cmd + "\"";
    }
#else
    const std::string cmd = magickCmd + " -background none \"" + svgPath.string() + "\"" +
                            resizeArg + " PNG32:\"" + cachePng.string() + "\" > /dev/null 2>&1";
#endif

    if (std::system(cmd.c_str()) == 0 && std::filesystem::exists(cachePng, ec)) {
        return LoadImage(cachePng.string().c_str());
    }

    return Image{};
}

void detectImageDimensions(const std::filesystem::path& filePath, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;

    // 1. Tentative rapide en mémoire via Raylib
    SetTraceLogLevel(LOG_ERROR);
    Image img = LoadImage(filePath.string().c_str());
    SetTraceLogLevel(LOG_INFO);

    if (IsImageValid(img)) {
        outWidth = img.width;
        outHeight = img.height;
        UnloadImage(img);
        return;
    }

    // 2. Fallback d'inspection d'en-tête (-ping) via ImageMagick pour WEBP/TIFF/HEIC/ICO
    const std::string magickCmd = Converter::resolveToolPath("magick");
    std::string cmd = magickCmd + " identify -ping -format \"%w %h\\n\" \"" + filePath.string() + "[0]\" 2>/dev/null";
#ifdef _WIN32
    cmd = magickCmd + " identify -ping -format \"%w %h\\n\" \"" + filePath.string() + "[0]\" 2>NUL";
    if (!cmd.empty() && cmd.front() == '"') {
        cmd = "\"" + cmd + "\"";
    }
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        return;
    }

    std::array<char, 128> buffer{};
    std::string output;
    if (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output = buffer.data();
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    int w = 0;
    int h = 0;
    if (std::sscanf(output.c_str(), "%d %d", &w, &h) == 2 && w > 0 && h > 0) {
        outWidth = w;
        outHeight = h;
    }
}

std::optional<std::filesystem::path> openNativeFileDialog() {
#ifdef _WIN32
    const char* cmd =
        "powershell -NoProfile -Command \""
        "Add-Type -AssemblyName System.Windows.Forms; "
        "$f = New-Object System.Windows.Forms.OpenFileDialog; "
        "$f.Title = 'Selectionner un fichier'; "
        "$f.Filter = 'Tous les fichiers supportes|*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif;*.heic;*.tiff;*.tif;*.ico;*.svg;*.dng;*.cr2;*.nef;*.arw;*.mp3;*.wav;*.flac;*.ogg;*.aac;*.m4a;*.mp4;*.mkv;*.avi;*.mov;*.webm;*.docx;*.odt;*.md;*.html;*.htm;*.txt|Tous les fichiers (*.*)|*.*'; "
        "if ($f.ShowDialog() -eq 'OK') { Write-Output $f.FileName }\"";
    FILE* pipe = _popen(cmd, "r");
#else
    const char* cmd =
        "if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --title=\"Sélectionner un fichier\" "
        "--file-filter=\"Fichiers supportés (Image, Audio, Vidéo, Document) | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.heic *.HEIC *.heif *.HEIF *.tiff *.TIFF *.tif *.TIF *.ico *.ICO *.svg *.SVG *.dng *.DNG *.cr2 *.CR2 *.nef *.NEF *.arw *.ARW *.mp3 *.MP3 *.wav *.WAV *.flac *.FLAC *.ogg *.OGG *.aac *.AAC *.m4a *.M4A *.mp4 *.MP4 *.mkv *.MKV *.avi *.AVI *.mov *.MOV *.webm *.WEBM *.docx *.DOCX *.odt *.ODT *.md *.MD *.html *.HTML *.htm *.HTM *.txt *.TXT\" "
        "--file-filter=\"Images | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.heic *.HEIC *.tiff *.TIFF *.tif *.TIF *.ico *.ICO *.svg *.SVG *.dng *.DNG *.cr2 *.CR2 *.nef *.NEF *.arw *.ARW\" "
        "--file-filter=\"Audio | *.mp3 *.MP3 *.wav *.WAV *.flac *.FLAC *.ogg *.OGG *.aac *.AAC *.m4a *.M4A\" "
        "--file-filter=\"Vidéo | *.mp4 *.MP4 *.mkv *.MKV *.avi *.AVI *.mov *.MOV *.webm *.WEBM\" "
        "--file-filter=\"Documents | *.docx *.DOCX *.odt *.ODT *.md *.MD *.html *.HTML *.htm *.HTM *.txt *.TXT\" "
        "--file-filter=\"Tous les fichiers | *\" 2>/dev/null; "
        "elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getopenfilename . \"*.png *.jpg *.jpeg *.webp *.bmp *.gif *.heic *.tiff *.tif *.ico *.svg *.dng *.cr2 *.nef *.arw *.mp3 *.wav *.flac *.ogg *.aac *.m4a *.mp4 *.mkv *.avi *.mov *.webm *.docx *.odt *.md *.html *.htm *.txt|Fichiers supportés\" 2>/dev/null; "
        "elif command -v yad >/dev/null 2>&1; then "
        "yad --file --title=\"Sélectionner un fichier\" 2>/dev/null; "
        "fi";
    FILE* pipe = popen(cmd, "r");
#endif
    if (!pipe) {
        return std::nullopt;
    }

    std::string output;
    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    if (output.empty()) {
        return std::nullopt;
    }

    return std::filesystem::path(output);
}

} // namespace

App::App() = default;

App::~App() {
    if (m_fileDialogTask.valid()) {
        m_fileDialogTask.wait();
    }
}

void App::loadAppFont() {
    const std::array<std::filesystem::path, 4> searchDirs = {
        "assets/fonts",
        "assets",
        "../assets/fonts",
        "../assets"
    };

    std::optional<std::filesystem::path> fontPath;
    for (const auto& dir : searchDirs) {
        fontPath = findFirstTtfInDir(dir);
        if (fontPath.has_value()) {
            break;
        }
    }

    if (fontPath.has_value()) {
        std::vector<int> codepoints;
        codepoints.reserve(224);
        for (int cp = 32; cp <= 255; ++cp) {
            codepoints.push_back(cp);
        }

        m_font = LoadFontEx(
            fontPath->string().c_str(),
            BASE_FONT_SIZE,
            codepoints.data(),
            static_cast<int>(codepoints.size())
        );

        if (IsFontValid(m_font) && m_font.texture.id != 0 && m_font.texture.id != GetFontDefault().texture.id) {
            SetTextureFilter(m_font.texture, TEXTURE_FILTER_BILINEAR);
            m_customFontLoaded = true;
            return;
        }
    }

    m_font = GetFontDefault();
    m_customFontLoaded = false;
}

void App::unloadAppFont() {
    if (m_customFontLoaded) {
        UnloadFont(m_font);
        m_customFontLoaded = false;
    }
}

void App::loadAppIcon() {
    const auto iconPath = findAssetFile("icon.svg");
    if (!iconPath.has_value()) {
        return;
    }

    Image iconImage = loadSvgImage(*iconPath, 64, 64);
    if (IsImageValid(iconImage)) {
        ImageFormat(&iconImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(iconImage);
        UnloadImage(iconImage);
    }
}

void App::loadAppLogo() {
    const auto logoPath = findAssetFile("logo.svg");
    if (!logoPath.has_value()) {
        return;
    }

    Image logoImage = loadSvgImage(*logoPath, 0, 12);
    if (IsImageValid(logoImage)) {
        m_logoTexture = LoadTextureFromImage(logoImage);
        UnloadImage(logoImage);

        if (m_logoTexture.id != 0) {
            SetTextureFilter(m_logoTexture, TEXTURE_FILTER_BILINEAR);
            m_logoLoaded = true;
        }
    }
}

void App::unloadAppLogo() {
    if (m_logoLoaded) {
        UnloadTexture(m_logoTexture);
        m_logoLoaded = false;
    }
}

void App::run() {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Convertix - Convertisseur de Fichiers");
    SetTargetFPS(60);

    loadAppIcon();
    loadAppFont();
    loadAppLogo();

    while (!WindowShouldClose()) {
        update();

        BeginDrawing();
        render();
        EndDrawing();
    }

    unloadAppLogo();
    unloadAppFont();
    CloseWindow();
}

void App::selectFile(const std::filesystem::path& filePath) {
    m_converter.reset();
    m_currentFile = filePath;
    m_detectedType = FileDetector::detect(m_currentFile);
    m_detectedCategory = FileDetector::getCategory(m_detectedType);
    m_availableTargets = FormatMatrix::getAvailableTargets(m_detectedType);
    m_selectedTargetIndex = 0;
    m_dropdownOpen = false;
    m_showSvgWarningModal = false;
    m_imageWidth = 0;
    m_imageHeight = 0;
    if (m_detectedCategory == FileCategory::IMAGE) {
        detectImageDimensions(m_currentFile, m_imageWidth, m_imageHeight);
    }
    m_pathScrollOffset = 0.0f;
    m_outputSizeFormatted.clear();
    m_state = AppState::FILE_DETECTED;
}

void App::openFileDialogAsync() {
    if (m_fileDialogOpen || m_state == AppState::CONVERTING) {
        return;
    }

    if (m_fileDialogTask.valid()) {
        m_fileDialogTask.wait();
    }

    m_fileDialogOpen = true;
    m_fileDialogTask = std::async(std::launch::async, []() {
        return openNativeFileDialog();
    });
}

void App::openOutputFolderAsync(const std::filesystem::path& outputPath) {
    if (outputPath.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::path normPath = std::filesystem::weakly_canonical(outputPath, ec);
    if (ec) {
        normPath = std::filesystem::absolute(outputPath, ec);
        if (ec) {
            normPath = outputPath;
        }
    }
    normPath.make_preferred();

#if defined(_WIN32)
    const std::string cmd = "explorer.exe /select,\"" + normPath.string() + "\"";
#else
    std::filesystem::path folderPath = normPath.parent_path();
    if (folderPath.empty()) {
        folderPath = ".";
    }
    const std::string cmd = "xdg-open \"" + folderPath.string() + "\" >/dev/null 2>&1";
#endif

    std::thread([cmd]() {
        std::system(cmd.c_str());
    }).detach();
}

void App::handleDroppedFiles() {
    if (!IsFileDropped()) {
        return;
    }

    FilePathList droppedFiles = LoadDroppedFiles();
    if (droppedFiles.count > 0 && m_state != AppState::CONVERTING && !m_showSvgWarningModal) {
        selectFile(std::filesystem::path(droppedFiles.paths[0]));
    }
    UnloadDroppedFiles(droppedFiles);
}

std::string App::formatFileSize(const std::filesystem::path& filePath) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(filePath, ec);
    if (ec) {
        return "";
    }

    std::ostringstream oss;
    constexpr double ONE_KB = 1024.0;
    constexpr double ONE_MB = 1024.0 * 1024.0;

    if (static_cast<double>(bytes) >= ONE_MB) {
        const double mb = static_cast<double>(bytes) / ONE_MB;
        oss << std::fixed << std::setprecision(1) << mb << " Mo";
    } else {
        const double kb = std::max(1.0, std::round(static_cast<double>(bytes) / ONE_KB));
        oss << static_cast<long long>(kb) << " Ko";
    }
    return oss.str();
}

void App::update() {
    handleDroppedFiles();

    if (m_fileDialogOpen && m_fileDialogTask.valid()) {
        if (m_fileDialogTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto selectedPath = m_fileDialogTask.get();
            m_fileDialogOpen = false;
            if (selectedPath.has_value() && m_state != AppState::CONVERTING) {
                selectFile(*selectedPath);
            }
        }
    }

    if (m_state == AppState::CONVERTING) {
        const ConversionStatus status = m_converter.getStatus();
        if (status == ConversionStatus::SUCCESS ||
            status == ConversionStatus::ERROR_CLI ||
            status == ConversionStatus::MISSING_TOOL) {
            if (status == ConversionStatus::SUCCESS) {
                m_outputSizeFormatted = formatFileSize(m_converter.getLastOutputPath());
            }
            m_pathScrollOffset = 0.0f;
            m_state = AppState::FINISHED;
        }
    }
}

void App::drawCenteredText(const std::string& text, float y, float fontSize, Color color) const {
    const Vector2 size = MeasureTextEx(m_font, text.c_str(), fontSize, TEXT_SPACING);
    const float x = (static_cast<float>(WINDOW_WIDTH) - size.x) / 2.0f;
    DrawTextEx(m_font, text.c_str(), {x, y}, fontSize, TEXT_SPACING, color);
}

bool App::drawButton(Rectangle bounds, const std::string& label, Color baseColor, Color hoverColor, Color textColor, bool enabled) const {
    const Vector2 mousePos = GetMousePosition();
    const bool hovered = enabled && CheckCollisionPointRec(mousePos, bounds);
    const Color currentColor = hovered ? hoverColor : baseColor;

    DrawRectangleRounded(bounds, 0.22f, 8, currentColor);
    DrawRectangleRoundedLinesEx(bounds, 0.22f, 8, 1.5f, Fade(BLACK, 0.18f));

    const float fontSize = 18.0f;
    const Vector2 textSize = MeasureTextEx(m_font, label.c_str(), fontSize, TEXT_SPACING);
    const float textX = bounds.x + (bounds.width - textSize.x) / 2.0f;
    const float textY = bounds.y + (bounds.height - textSize.y) / 2.0f;
    DrawTextEx(m_font, label.c_str(), {textX, textY}, fontSize, TEXT_SPACING, textColor);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

std::string App::fitTextToWidth(const std::string& text, float fontSize, float maxWidth, bool keepEnd) const {
    if (MeasureTextEx(m_font, text.c_str(), fontSize, TEXT_SPACING).x <= maxWidth) {
        return text;
    }

    const std::string ellipsis = "...";
    if (!keepEnd) {
        std::string truncated = text;
        while (!truncated.empty() &&
               MeasureTextEx(m_font, (truncated + ellipsis).c_str(), fontSize, TEXT_SPACING).x > maxWidth) {
            while (!truncated.empty() && (static_cast<unsigned char>(truncated.back()) & 0xC0) == 0x80) {
                truncated.pop_back();
            }
            if (!truncated.empty()) {
                truncated.pop_back();
            }
        }
        return truncated + ellipsis;
    } else {
        std::string suffix = text;
        while (!suffix.empty() &&
               MeasureTextEx(m_font, (ellipsis + suffix).c_str(), fontSize, TEXT_SPACING).x > maxWidth) {
            suffix.erase(0, 1);
            while (!suffix.empty() && (static_cast<unsigned char>(suffix.front()) & 0xC0) == 0x80) {
                suffix.erase(0, 1);
            }
        }
        return ellipsis + suffix;
    }
}

void App::render() {
    ClearBackground({245, 247, 250, 255});

    // Logo discret en haut à gauche de la fenêtre (12px de haut)
    if (m_logoLoaded) {
        DrawTexture(m_logoTexture, 36, 12, WHITE);
    }

    // Zone principale de glisser-déposer
    const Rectangle dropZone{36.0f, 34.0f, static_cast<float>(WINDOW_WIDTH - 72), 330.0f};
    DrawRectangleRounded(dropZone, 0.06f, 8, RAYWHITE);
    drawDashedRect(dropZone, 12.0f, 8.0f, 2.0f, {148, 163, 184, 255});

    switch (m_state) {
        case AppState::WAITING_DROP: {
            drawCenteredText("Glisser un fichier ici", 138.0f, 24.0f, {51, 65, 85, 255});
            drawCenteredText("- ou -", 188.0f, 16.0f, {148, 163, 184, 255});

            const Rectangle importBtn{210.0f, 224.0f, 180.0f, 44.0f};
            const std::string importLabel = m_fileDialogOpen ? "Ouverture..." : "Importer";
            const Color baseCol = m_fileDialogOpen ? Color{148, 163, 184, 255} : Color{37, 99, 235, 255};
            if (drawButton(importBtn, importLabel, baseCol, {29, 78, 216, 255}, RAYWHITE, !m_fileDialogOpen)) {
                openFileDialogAsync();
            }
            break;
        }

        case AppState::FILE_DETECTED: {
            const std::string rawFileName = m_currentFile.filename().string();
            const std::string displayFileName = fitTextToWidth("Fichier : " + rawFileName, 20.0f, dropZone.width - 40.0f, false);
            drawCenteredText(displayFileName, 52.0f, 20.0f, {30, 41, 59, 255});

            if (m_detectedType == FileType::UNKNOWN || m_availableTargets.empty()) {
                drawCenteredText("Format non supporté", 130.0f, 22.0f, {220, 38, 38, 255});
                drawCenteredText("Signature binaire (Magic Bytes) non reconnue.", 165.0f, 16.0f, {100, 116, 139, 255});

                const Rectangle resetBtn{200.0f, 235.0f, 200.0f, 42.0f};
                if (drawButton(resetBtn, "Réinitialiser", {71, 85, 105, 255}, {51, 65, 85, 255}, RAYWHITE)) {
                    resetState();
                }
            } else {
                const std::string detectedStr =
                    "Catégorie : " + FileDetector::categoryToString(m_detectedCategory) +
                    "  |  Format détecté : " + FileDetector::toString(m_detectedType);
                drawCenteredText(detectedStr, 84.0f, 18.0f, {22, 163, 74, 255});
                drawCenteredText("Sélectionnez le format cible :", 114.0f, 16.0f, {100, 116, 139, 255});

                const Rectangle dropdownBox{115.0f, 142.0f, 200.0f, 36.0f};
                const Rectangle convertBtn{330.0f, 142.0f, 155.0f, 36.0f};
                const Rectangle cancelBtn{220.0f, 308.0f, 160.0f, 36.0f};

                const bool bgControlsEnabled = !m_dropdownOpen && !m_showSvgWarningModal;

                if (drawButton(convertBtn, "Convertir", {37, 99, 235, 255}, {29, 78, 216, 255}, RAYWHITE, bgControlsEnabled)) {
                    if (!m_availableTargets.empty() && m_selectedTargetIndex < m_availableTargets.size()) {
                        const FileType chosenTarget = m_availableTargets[m_selectedTargetIndex];
                        m_dropdownOpen = false;
                        if (chosenTarget == FileType::SVG && m_detectedCategory == FileCategory::IMAGE) {
                            m_showSvgWarningModal = true;
                        } else {
                            m_converter.startConversion(m_currentFile, m_detectedType, chosenTarget);
                            m_state = AppState::CONVERTING;
                        }
                        break;
                    }
                }

                if (drawButton(cancelBtn, "Réinitialiser", {203, 213, 225, 255}, {148, 163, 184, 255}, {30, 41, 59, 255}, bgControlsEnabled)) {
                    resetState();
                    break;
                }

                const Vector2 mousePos = GetMousePosition();
                const bool headerHovered = !m_showSvgWarningModal && CheckCollisionPointRec(mousePos, dropdownBox);
                const Color headerBg = headerHovered ? Color{241, 245, 249, 255} : RAYWHITE;
                const Color borderCol = m_dropdownOpen ? Color{37, 99, 235, 255} : Color{148, 163, 184, 255};

                DrawRectangleRounded(dropdownBox, 0.2f, 8, headerBg);
                DrawRectangleRoundedLinesEx(dropdownBox, 0.2f, 8, 1.8f, borderCol);

                const std::string selectedLabel = m_availableTargets.empty()
                    ? "-"
                    : FileDetector::toString(m_availableTargets[m_selectedTargetIndex]);
                const float itemFontSize = 16.0f;
                const Vector2 selTextSize = MeasureTextEx(m_font, selectedLabel.c_str(), itemFontSize, TEXT_SPACING);
                DrawTextEx(
                    m_font,
                    selectedLabel.c_str(),
                    {dropdownBox.x + 16.0f, dropdownBox.y + (dropdownBox.height - selTextSize.y) / 2.0f},
                    itemFontSize,
                    TEXT_SPACING,
                    {30, 41, 59, 255}
                );

                const float arrowCenterX = dropdownBox.x + dropdownBox.width - 22.0f;
                const float arrowCenterY = dropdownBox.y + dropdownBox.height / 2.0f;
                if (m_dropdownOpen) {
                    DrawTriangle(
                        {arrowCenterX - 6.0f, arrowCenterY + 3.0f},
                        {arrowCenterX + 6.0f, arrowCenterY + 3.0f},
                        {arrowCenterX, arrowCenterY - 4.0f},
                        {51, 65, 85, 255}
                    );
                } else {
                    DrawTriangle(
                        {arrowCenterX - 6.0f, arrowCenterY - 3.0f},
                        {arrowCenterX, arrowCenterY + 4.0f},
                        {arrowCenterX + 6.0f, arrowCenterY - 3.0f},
                        {51, 65, 85, 255}
                    );
                }

                bool clickedInsideDropdown = false;
                if (m_dropdownOpen && !m_availableTargets.empty()) {
                    // Construction des lignes d'affichage avec séparateurs visuels si la source est une Vidéo
                    std::vector<DropdownRow> rows;
                    if (m_detectedCategory == FileCategory::VIDEO) {
                        bool addedVideoHeader = false;
                        bool addedAudioHeader = false;
                        for (std::size_t i = 0; i < m_availableTargets.size(); ++i) {
                            const FileCategory targetCat = FileDetector::getCategory(m_availableTargets[i]);
                            if (targetCat == FileCategory::VIDEO && !addedVideoHeader) {
                                rows.push_back({true, "--- VIDÉO ---", 0});
                                addedVideoHeader = true;
                            } else if (targetCat == FileCategory::AUDIO && !addedAudioHeader) {
                                rows.push_back({true, "--- AUDIO (EXTRACTION) ---", 0});
                                addedAudioHeader = true;
                            }
                            rows.push_back({false, FileDetector::toString(m_availableTargets[i]), i});
                        }
                    } else {
                        for (std::size_t i = 0; i < m_availableTargets.size(); ++i) {
                            rows.push_back({false, FileDetector::toString(m_availableTargets[i]), i});
                        }
                    }

                    const float sepHeight = 20.0f;
                    const float optHeight = (rows.size() > 7) ? 21.5f : 26.0f;

                    float listHeight = 0.0f;
                    for (const auto& r : rows) {
                        listHeight += r.isSeparator ? sepHeight : optHeight;
                    }

                    const Rectangle listBounds{dropdownBox.x, dropdownBox.y + dropdownBox.height + 3.0f, dropdownBox.width, listHeight};

                    DrawRectangleRounded({listBounds.x + 2.0f, listBounds.y + 3.0f, listBounds.width, listBounds.height}, 0.08f, 8, Fade(BLACK, 0.12f));
                    DrawRectangleRec(listBounds, RAYWHITE);

                    float curY = listBounds.y;
                    for (const auto& row : rows) {
                        const float h = row.isSeparator ? sepHeight : optHeight;
                        const Rectangle itemRec{listBounds.x, curY, listBounds.width, h};

                        if (row.isSeparator) {
                            DrawRectangleRec(itemRec, {241, 245, 249, 255});
                            const float sepFontSize = 13.0f;
                            const Vector2 sepSize = MeasureTextEx(m_font, row.label.c_str(), sepFontSize, TEXT_SPACING);
                            DrawTextEx(
                                m_font,
                                row.label.c_str(),
                                {itemRec.x + (itemRec.width - sepSize.x) / 2.0f, itemRec.y + (h - sepSize.y) / 2.0f},
                                sepFontSize,
                                TEXT_SPACING,
                                DARKGRAY
                            );

                            if (CheckCollisionPointRec(mousePos, itemRec) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                                clickedInsideDropdown = true;
                            }
                        } else {
                            const bool itemHovered = CheckCollisionPointRec(mousePos, itemRec);
                            if (itemHovered) {
                                DrawRectangleRec(itemRec, {219, 234, 254, 255});
                            } else if (row.targetIndex == m_selectedTargetIndex) {
                                DrawRectangleRec(itemRec, {248, 250, 252, 255});
                            }

                            const Vector2 optSize = MeasureTextEx(m_font, row.label.c_str(), itemFontSize, TEXT_SPACING);
                            const Color optColor = (row.targetIndex == m_selectedTargetIndex)
                                ? Color{29, 78, 216, 255}
                                : Color{30, 41, 59, 255};

                            DrawTextEx(
                                m_font,
                                row.label.c_str(),
                                {itemRec.x + 16.0f, itemRec.y + (h - optSize.y) / 2.0f},
                                itemFontSize,
                                TEXT_SPACING,
                                optColor
                            );

                            if (itemHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                                m_selectedTargetIndex = row.targetIndex;
                                m_dropdownOpen = false;
                                clickedInsideDropdown = true;
                            }
                        }
                        curY += h;
                    }

                    DrawRectangleLinesEx(listBounds, 1.5f, {148, 163, 184, 255});
                }

                if (!m_showSvgWarningModal && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickedInsideDropdown) {
                    if (headerHovered) {
                        m_dropdownOpen = !m_dropdownOpen;
                    } else if (m_dropdownOpen) {
                        m_dropdownOpen = false;
                    }
                }
            }
            break;
        }

        case AppState::CONVERTING: {
            if (m_detectedCategory == FileCategory::VIDEO) {
                const float progress = std::clamp(m_converter.getProgress(), 0.0f, 100.0f);
                const int pctInt = static_cast<int>(std::round(progress));
                const std::string titleText = "Conversion vidéo... " + std::to_string(pctInt) + "%";

                drawCenteredText(titleText, 140.0f, 22.0f, {30, 41, 59, 255});

                // Barre de progression centrée
                const Rectangle barBg{120.0f, 182.0f, 360.0f, 26.0f};
                DrawRectangleRounded(barBg, 0.35f, 8, {226, 232, 240, 255});

                if (progress > 0.5f) {
                    const float fillW = std::max(14.0f, (barBg.width - 4.0f) * (progress / 100.0f));
                    const Rectangle barFill{barBg.x + 2.0f, barBg.y + 2.0f, fillW, barBg.height - 4.0f};
                    DrawRectangleRounded(barFill, 0.35f, 8, {37, 99, 235, 255});
                } else {
                    // Fallback animé tant que les premières trames sont en cours d'analyse
                    const float t = static_cast<float>(GetTime());
                    const float pulseW = 80.0f;
                    const float maxOffset = barBg.width - pulseW - 4.0f;
                    const float offset = (std::sin(t * 3.5f) * 0.5f + 0.5f) * maxOffset;
                    const Rectangle barPulse{barBg.x + 2.0f + offset, barBg.y + 2.0f, pulseW, barBg.height - 4.0f};
                    DrawRectangleRounded(barPulse, 0.35f, 8, {59, 130, 246, 255});
                }

                DrawRectangleRoundedLinesEx(barBg, 0.35f, 8, 1.5f, {148, 163, 184, 255});
                drawCenteredText("Encodage en arrière-plan via FFmpeg", 224.0f, 16.0f, {100, 116, 139, 255});
            } else {
                const float time = static_cast<float>(GetTime());
                const float startAngle = time * 360.0f;
                DrawRing({static_cast<float>(WINDOW_WIDTH) / 2.0f, 155.0f}, 18.0f, 24.0f, startAngle, startAngle + 270.0f, 32, {37, 99, 235, 255});

                drawCenteredText("Conversion en cours...", 205.0f, 22.0f, {30, 41, 59, 255});
                std::string engineSub = "Traitement asynchrone via ImageMagick";
                if (m_detectedCategory == FileCategory::IMAGE &&
                    !m_availableTargets.empty() &&
                    m_selectedTargetIndex < m_availableTargets.size() &&
                    m_availableTargets[m_selectedTargetIndex] == FileType::SVG) {
                    engineSub = "Vectorisation couleur asynchrone via VTracer";
                } else if (m_detectedCategory == FileCategory::AUDIO) {
                    engineSub = "Traitement asynchrone via FFmpeg";
                } else if (m_detectedCategory == FileCategory::DOCUMENT) {
                    engineSub = "Traitement asynchrone via Pandoc";
                }
                drawCenteredText(engineSub, 238.0f, 16.0f, {100, 116, 139, 255});
            }
            break;
        }

        case AppState::FINISHED: {
            const ConversionStatus status = m_converter.getStatus();
            if (status == ConversionStatus::SUCCESS) {
                const std::filesystem::path outPath = m_converter.getLastOutputPath();
                std::error_code ec;
                const std::filesystem::path absPath = std::filesystem::weakly_canonical(outPath, ec);
                const std::string rawPathStr = ec ? outPath.string() : absPath.string();

                std::string successHeadline = "Succès ! Fichier créé";
                if (!m_outputSizeFormatted.empty()) {
                    successHeadline += " (" + m_outputSizeFormatted + ")";
                }
                drawCenteredText(successHeadline, 78.0f, 22.0f, {22, 163, 74, 255});
                drawCenteredText("Chemin du fichier généré :", 114.0f, 15.0f, {100, 116, 139, 255});

                // Zone conteneur du chemin avec police réduite et défilement horizontal (Scissor Mode)
                const Rectangle pathBox{60.0f, 140.0f, 480.0f, 38.0f};
                DrawRectangleRounded(pathBox, 0.2f, 8, {241, 245, 249, 255});
                DrawRectangleRoundedLinesEx(pathBox, 0.2f, 8, 1.2f, {203, 213, 225, 255});

                const float pathFontSize = 15.0f;
                const Vector2 fullTextSize = MeasureTextEx(m_font, rawPathStr.c_str(), pathFontSize, TEXT_SPACING);
                const float innerPadding = 22.0f;
                const float visibleWidth = pathBox.width - innerPadding * 2.0f;
                const float textY = pathBox.y + (pathBox.height - fullTextSize.y) / 2.0f;

                if (fullTextSize.x <= visibleWidth) {
                    DrawTextEx(
                        m_font,
                        rawPathStr.c_str(),
                        {pathBox.x + (pathBox.width - fullTextSize.x) / 2.0f, textY},
                        pathFontSize,
                        TEXT_SPACING,
                        {30, 41, 59, 255}
                    );
                } else {
                    const float maxScroll = fullTextSize.x - visibleWidth;
                    const Vector2 mousePos = GetMousePosition();
                    if (CheckCollisionPointRec(mousePos, pathBox)) {
                        const float wheel = GetMouseWheelMove();
                        if (wheel != 0.0f) {
                            m_pathScrollOffset -= wheel * 36.0f;
                        }
                    }
                    if (IsKeyDown(KEY_RIGHT)) {
                        m_pathScrollOffset += 4.0f;
                    }
                    if (IsKeyDown(KEY_LEFT)) {
                        m_pathScrollOffset -= 4.0f;
                    }
                    m_pathScrollOffset = std::clamp(m_pathScrollOffset, 0.0f, maxScroll);

                    // Indicateurs latéraux discrets < et >
                    if (m_pathScrollOffset > 1.0f) {
                        DrawTextEx(m_font, "<", {pathBox.x + 6.0f, textY}, 14.0f, TEXT_SPACING, {100, 116, 139, 255});
                    }
                    if (m_pathScrollOffset < maxScroll - 1.0f) {
                        DrawTextEx(m_font, ">", {pathBox.x + pathBox.width - 14.0f, textY}, 14.0f, TEXT_SPACING, {100, 116, 139, 255});
                    }

                    BeginScissorMode(
                        static_cast<int>(pathBox.x + innerPadding),
                        static_cast<int>(pathBox.y),
                        static_cast<int>(visibleWidth),
                        static_cast<int>(pathBox.height)
                    );
                    DrawTextEx(
                        m_font,
                        rawPathStr.c_str(),
                        {pathBox.x + innerPadding - m_pathScrollOffset, textY},
                        pathFontSize,
                        TEXT_SPACING,
                        {30, 41, 59, 255}
                    );
                    EndScissorMode();

                    // Barre de défilement horizontale miniature sous le conteneur
                    const Rectangle trackRec{pathBox.x + innerPadding, pathBox.y + pathBox.height + 4.0f, visibleWidth, 4.0f};
                    DrawRectangleRounded(trackRec, 0.5f, 4, {226, 232, 240, 255});
                    const float thumbRatio = visibleWidth / fullTextSize.x;
                    const float thumbW = std::max(30.0f, trackRec.width * thumbRatio);
                    const float thumbX = trackRec.x + (trackRec.width - thumbW) * (m_pathScrollOffset / maxScroll);
                    DrawRectangleRounded({thumbX, trackRec.y, thumbW, trackRec.height}, 0.5f, 4, {148, 163, 184, 255});
                }
            } else if (status == ConversionStatus::MISSING_TOOL) {
                drawCenteredText("Outil CLI manquant", 115.0f, 22.0f, {220, 38, 38, 255});
                std::string missingMsg = "Veuillez installer ImageMagick ('magick') ou le placer dans bin/imagemagick/.";
                if (m_detectedCategory == FileCategory::IMAGE &&
                    !m_availableTargets.empty() &&
                    m_selectedTargetIndex < m_availableTargets.size() &&
                    m_availableTargets[m_selectedTargetIndex] == FileType::SVG) {
                    missingMsg = "Veuillez installer VTracer ('vtracer') ou le placer dans bin/vtracer/.";
                } else if (m_detectedCategory == FileCategory::AUDIO || m_detectedCategory == FileCategory::VIDEO) {
                    missingMsg = "Veuillez installer FFmpeg ('ffmpeg') ou le placer dans bin/ffmpeg/.";
                } else if (m_detectedCategory == FileCategory::DOCUMENT) {
                    missingMsg = "Veuillez installer Pandoc ('pandoc') ou le placer dans bin/pandoc/.";
                }
                drawCenteredText(missingMsg, 152.0f, 15.0f, {100, 116, 139, 255});
            } else {
                drawCenteredText("Erreur lors de la conversion", 115.0f, 22.0f, {220, 38, 38, 255});
                drawCenteredText("La commande CLI externe a échoué.", 152.0f, 16.0f, {100, 116, 139, 255});
            }

            drawCenteredText("Glissez un autre fichier ou choisissez une action :", 214.0f, 16.0f, {100, 116, 139, 255});

            if (status == ConversionStatus::SUCCESS && !m_converter.getLastOutputPath().empty()) {
                const Rectangle openFolderBtn{58.0f, 252.0f, 156.0f, 42.0f};
                const Rectangle reconvertBtn{226.0f, 252.0f, 148.0f, 42.0f};
                const Rectangle newFileBtn{386.0f, 252.0f, 156.0f, 42.0f};

                if (drawButton(openFolderBtn, "Ouvrir le dossier", {22, 163, 74, 255}, {21, 128, 61, 255}, RAYWHITE)) {
                    openOutputFolderAsync(m_converter.getLastOutputPath());
                }
                if (drawButton(reconvertBtn, "Autre format", {37, 99, 235, 255}, {29, 78, 216, 255}, RAYWHITE)) {
                    returnToFormatSelection();
                }
                if (drawButton(newFileBtn, "Nouveau fichier", {71, 85, 105, 255}, {51, 65, 85, 255}, RAYWHITE)) {
                    resetState();
                }
            } else {
                const Rectangle reconvertBtn{115.0f, 252.0f, 175.0f, 42.0f};
                const Rectangle newFileBtn{310.0f, 252.0f, 175.0f, 42.0f};

                if (drawButton(reconvertBtn, "Autre format", {37, 99, 235, 255}, {29, 78, 216, 255}, RAYWHITE)) {
                    returnToFormatSelection();
                }
                if (drawButton(newFileBtn, "Nouveau fichier", {71, 85, 105, 255}, {51, 65, 85, 255}, RAYWHITE)) {
                    resetState();
                }
            }
            break;
        }
    }

    // Modale d'avertissement avant vectorisation matricielle -> SVG
    if (m_showSvgWarningModal) {
        DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(BLACK, 0.65f));

        const bool isLowRes = (m_imageWidth > 0 && m_imageHeight > 0 &&
                               (m_imageWidth < 256 || m_imageHeight < 256));
        const Rectangle modalBox = isLowRes
            ? Rectangle{35.0f, 26.0f, 530.0f, 348.0f}
            : Rectangle{45.0f, 68.0f, 510.0f, 264.0f};

        DrawRectangleRounded(modalBox, 0.06f, 8, {30, 41, 59, 255});
        DrawRectangleRoundedLinesEx(modalBox, 0.06f, 8, 2.0f, RAYWHITE);

        const float baseY = modalBox.y + 18.0f;
        drawCenteredText("Attention : Vectorisation (-> SVG)", baseY, 20.0f, {251, 191, 36, 255});

        drawCenteredText("Vous allez convertir une image matricielle en tracé vectoriel.", baseY + 34.0f, 15.0f, RAYWHITE);
        drawCenteredText("Recommandé pour : logos, icônes, croquis, illustrations", baseY + 60.0f, 14.5f, {134, 239, 172, 255});
        drawCenteredText("et formes nettes (couleurs préservées via VTracer).", baseY + 78.0f, 14.5f, {134, 239, 172, 255});
        drawCenteredText("Déconseillé pour : photos réalistes, visages, paysages,", baseY + 104.0f, 14.5f, {252, 165, 165, 255});
        drawCenteredText("dégradés complexes (risque de fichier lourd et perte de détails).", baseY + 122.0f, 14.5f, {252, 165, 165, 255});

        // Uniquement si largeur < 256 OU hauteur < 256 : encart d'alerte rouge/orange percutant
        if (isLowRes) {
            const Rectangle alertBox{55.0f, baseY + 150.0f, 490.0f, 86.0f};
            DrawRectangleRounded(alertBox, 0.14f, 8, {69, 20, 20, 245});
            DrawRectangleRoundedLinesEx(alertBox, 0.14f, 8, 2.0f, {249, 115, 22, 255});

            drawCenteredText(
                "ATTENTION : IMAGE TRÈS PETITE",
                alertBox.y + 10.0f,
                16.5f,
                {251, 146, 60, 255}
            );

            const std::string resLine =
                "Résolution détectée : " + std::to_string(m_imageWidth) + "x" + std::to_string(m_imageHeight) + " px";
            drawCenteredText(
                resLine,
                alertBox.y + 34.0f,
                15.0f,
                RAYWHITE
            );

            drawCenteredText(
                "Le tracé SVG sera déformé ou illisible (minimum conseillé : 512x512 px).",
                alertBox.y + 58.0f,
                13.5f,
                {254, 215, 170, 255}
            );
        }

        const float btnY = modalBox.y + modalBox.height - 54.0f;
        const Rectangle cancelModalBtn{125.0f, btnY, 160.0f, 40.0f};
        const Rectangle continueModalBtn{315.0f, btnY, 160.0f, 40.0f};

        if (drawButton(cancelModalBtn, "Annuler", {71, 85, 105, 255}, {100, 116, 139, 255}, RAYWHITE)) {
            m_showSvgWarningModal = false;
        }
        if (drawButton(continueModalBtn, "Continuer", {37, 99, 235, 255}, {29, 78, 216, 255}, RAYWHITE)) {
            m_showSvgWarningModal = false;
            if (!m_availableTargets.empty() && m_selectedTargetIndex < m_availableTargets.size()) {
                m_converter.startConversion(m_currentFile, m_detectedType, m_availableTargets[m_selectedTargetIndex]);
                m_state = AppState::CONVERTING;
            }
        }
    }
}

void App::returnToFormatSelection() {
    m_converter.reset();
    m_dropdownOpen = false;
    m_showSvgWarningModal = false;
    m_pathScrollOffset = 0.0f;
    m_outputSizeFormatted.clear();
    m_state = AppState::FILE_DETECTED;
}

void App::resetState() {
    m_converter.reset();
    m_currentFile.clear();
    m_detectedType = FileType::UNKNOWN;
    m_detectedCategory = FileCategory::UNKNOWN;
    m_availableTargets.clear();
    m_selectedTargetIndex = 0;
    m_dropdownOpen = false;
    m_showSvgWarningModal = false;
    m_imageWidth = 0;
    m_imageHeight = 0;
    m_pathScrollOffset = 0.0f;
    m_outputSizeFormatted.clear();
    m_state = AppState::WAITING_DROP;
}
