#include "App.hpp"
#include "FormatMatrix.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr float TEXT_SPACING = 1.0f;

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

    // 1. Chargement instantané depuis le cache PNG si déjà rastérisé et à jour
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

    // 2. Sinon, rastérisation unique vers le fichier de cache PNG
    std::string resizeArg;
    if (targetWidth > 0 && targetHeight > 0) {
        resizeArg = " -resize " + std::to_string(targetWidth) + "x" + std::to_string(targetHeight);
    } else if (targetHeight > 0) {
        resizeArg = " -resize x" + std::to_string(targetHeight);
    }

#ifdef _WIN32
    const std::string cmd = "magick -background none \"" + svgPath.string() + "\"" +
                            resizeArg + " PNG32:\"" + cachePng.string() + "\" > NUL 2>&1";
#else
    const std::string cmd = "magick -background none \"" + svgPath.string() + "\"" +
                            resizeArg + " PNG32:\"" + cachePng.string() + "\" > /dev/null 2>&1";
#endif

    if (std::system(cmd.c_str()) == 0 && std::filesystem::exists(cachePng, ec)) {
        return LoadImage(cachePng.string().c_str());
    }

    return Image{};
}

std::optional<std::filesystem::path> openNativeFileDialog() {
#ifdef _WIN32
    const char* cmd =
        "powershell -NoProfile -Command \""
        "Add-Type -AssemblyName System.Windows.Forms; "
        "$f = New-Object System.Windows.Forms.OpenFileDialog; "
        "$f.Title = 'Selectionner un fichier media'; "
        "$f.Filter = 'Medias supportes (*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif;*.heic;*.tiff;*.tif;*.ico;*.svg;*.dng;*.cr2;*.nef;*.arw;*.mp3;*.wav;*.flac;*.ogg;*.aac;*.m4a)|*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif;*.heic;*.tiff;*.tif;*.ico;*.svg;*.dng;*.cr2;*.nef;*.arw;*.mp3;*.wav;*.flac;*.ogg;*.aac;*.m4a|Tous les fichiers (*.*)|*.*'; "
        "if ($f.ShowDialog() -eq 'OK') { Write-Output $f.FileName }\"";
    FILE* pipe = _popen(cmd, "r");
#else
    const char* cmd =
        "if command -v zenity >/dev/null 2>&1; then "
        "zenity --file-selection --title=\"Sélectionner un fichier\" "
        "--file-filter=\"Médias (Images & Audio) | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.heic *.HEIC *.heif *.HEIF *.tiff *.TIFF *.tif *.TIF *.ico *.ICO *.svg *.SVG *.dng *.DNG *.cr2 *.CR2 *.nef *.NEF *.arw *.ARW *.mp3 *.MP3 *.wav *.WAV *.flac *.FLAC *.ogg *.OGG *.aac *.AAC *.m4a *.M4A\" "
        "--file-filter=\"Images | *.png *.PNG *.jpg *.JPG *.jpeg *.JPEG *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.heic *.HEIC *.tiff *.TIFF *.tif *.TIF *.ico *.ICO *.svg *.SVG *.dng *.DNG *.cr2 *.CR2 *.nef *.NEF *.arw *.ARW\" "
        "--file-filter=\"Audio | *.mp3 *.MP3 *.wav *.WAV *.flac *.FLAC *.ogg *.OGG *.aac *.AAC *.m4a *.M4A\" "
        "--file-filter=\"Tous les fichiers | *\" 2>/dev/null; "
        "elif command -v kdialog >/dev/null 2>&1; then "
        "kdialog --getopenfilename . \"*.png *.jpg *.jpeg *.webp *.bmp *.gif *.heic *.tiff *.tif *.ico *.svg *.dng *.cr2 *.nef *.arw *.mp3 *.wav *.flac *.ogg *.aac *.m4a|Médias\" 2>/dev/null; "
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

void App::handleDroppedFiles() {
    if (!IsFileDropped()) {
        return;
    }

    FilePathList droppedFiles = LoadDroppedFiles();
    if (droppedFiles.count > 0 && m_state != AppState::CONVERTING) {
        selectFile(std::filesystem::path(droppedFiles.paths[0]));
    }
    UnloadDroppedFiles(droppedFiles);
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
            drawCenteredText("Glissez et déposez un fichier ici", 112.0f, 22.0f, {51, 65, 85, 255});
            drawCenteredText("Images : PNG, JPG, WEBP, SVG, HEIC, TIFF, ICO, RAW, GIF", 148.0f, 15.0f, {100, 116, 139, 255});
            drawCenteredText("Audio : MP3, WAV, FLAC, OGG, AAC, M4A", 170.0f, 15.0f, {100, 116, 139, 255});
            drawCenteredText("- ou -", 204.0f, 16.0f, {148, 163, 184, 255});

            const Rectangle importBtn{210.0f, 236.0f, 180.0f, 44.0f};
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
            drawCenteredText(displayFileName, 60.0f, 20.0f, {30, 41, 59, 255});

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
                    "  |  Format réel : " + FileDetector::toString(m_detectedType);
                drawCenteredText(detectedStr, 94.0f, 18.0f, {22, 163, 74, 255});
                drawCenteredText("Sélectionnez le format cible :", 128.0f, 16.0f, {100, 116, 139, 255});

                const Rectangle dropdownBox{125.0f, 158.0f, 185.0f, 40.0f};
                const Rectangle convertBtn{325.0f, 158.0f, 150.0f, 40.0f};
                const Rectangle cancelBtn{220.0f, 294.0f, 160.0f, 36.0f};

                if (drawButton(convertBtn, "Convertir", {37, 99, 235, 255}, {29, 78, 216, 255}, RAYWHITE, !m_dropdownOpen)) {
                    if (!m_availableTargets.empty() && m_selectedTargetIndex < m_availableTargets.size()) {
                        m_converter.startConversion(m_currentFile, m_detectedType, m_availableTargets[m_selectedTargetIndex]);
                        m_dropdownOpen = false;
                        m_state = AppState::CONVERTING;
                        break;
                    }
                }

                if (drawButton(cancelBtn, "Réinitialiser", {203, 213, 225, 255}, {148, 163, 184, 255}, {30, 41, 59, 255}, !m_dropdownOpen)) {
                    resetState();
                    break;
                }

                const Vector2 mousePos = GetMousePosition();
                const bool headerHovered = CheckCollisionPointRec(mousePos, dropdownBox);
                const Color headerBg = headerHovered ? Color{241, 245, 249, 255} : RAYWHITE;
                const Color borderCol = m_dropdownOpen ? Color{37, 99, 235, 255} : Color{148, 163, 184, 255};

                DrawRectangleRounded(dropdownBox, 0.2f, 8, headerBg);
                DrawRectangleRoundedLinesEx(dropdownBox, 0.2f, 8, 1.8f, borderCol);

                const std::string selectedLabel = m_availableTargets.empty()
                    ? "-"
                    : FileDetector::toString(m_availableTargets[m_selectedTargetIndex]);
                const float itemFontSize = 18.0f;
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
                    const float itemHeight = 28.0f;
                    const float listHeight = static_cast<float>(m_availableTargets.size()) * itemHeight;
                    const Rectangle listBounds{dropdownBox.x, dropdownBox.y + dropdownBox.height + 3.0f, dropdownBox.width, listHeight};

                    DrawRectangleRounded({listBounds.x + 2.0f, listBounds.y + 3.0f, listBounds.width, listBounds.height}, 0.12f, 8, Fade(BLACK, 0.12f));
                    DrawRectangleRec(listBounds, RAYWHITE);

                    for (std::size_t i = 0; i < m_availableTargets.size(); ++i) {
                        const Rectangle itemRec{
                            listBounds.x,
                            listBounds.y + static_cast<float>(i) * itemHeight,
                            listBounds.width,
                            itemHeight
                        };
                        const bool itemHovered = CheckCollisionPointRec(mousePos, itemRec);
                        if (itemHovered) {
                            DrawRectangleRec(itemRec, {219, 234, 254, 255});
                        } else if (i == m_selectedTargetIndex) {
                            DrawRectangleRec(itemRec, {241, 245, 249, 255});
                        }

                        const std::string optText = FileDetector::toString(m_availableTargets[i]);
                        const Vector2 optSize = MeasureTextEx(m_font, optText.c_str(), itemFontSize, TEXT_SPACING);
                        const Color optColor = (i == m_selectedTargetIndex)
                            ? Color{29, 78, 216, 255}
                            : Color{30, 41, 59, 255};

                        DrawTextEx(
                            m_font,
                            optText.c_str(),
                            {itemRec.x + 16.0f, itemRec.y + (itemHeight - optSize.y) / 2.0f},
                            itemFontSize,
                            TEXT_SPACING,
                            optColor
                        );

                        if (itemHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            m_selectedTargetIndex = i;
                            m_dropdownOpen = false;
                            clickedInsideDropdown = true;
                        }
                    }

                    DrawRectangleLinesEx(listBounds, 1.5f, {148, 163, 184, 255});
                }

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !clickedInsideDropdown) {
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
            const float time = static_cast<float>(GetTime());
            const float startAngle = time * 360.0f;
            DrawRing({static_cast<float>(WINDOW_WIDTH) / 2.0f, 155.0f}, 18.0f, 24.0f, startAngle, startAngle + 270.0f, 32, {37, 99, 235, 255});

            drawCenteredText("Conversion en cours...", 205.0f, 22.0f, {30, 41, 59, 255});
            const std::string engineSub = (m_detectedCategory == FileCategory::AUDIO || m_detectedCategory == FileCategory::VIDEO)
                ? "Traitement asynchrone via FFmpeg"
                : "Traitement asynchrone via ImageMagick";
            drawCenteredText(engineSub, 238.0f, 16.0f, {100, 116, 139, 255});
            break;
        }

        case AppState::FINISHED: {
            const ConversionStatus status = m_converter.getStatus();
            if (status == ConversionStatus::SUCCESS) {
                const std::filesystem::path outPath = m_converter.getLastOutputPath();
                std::error_code ec;
                const std::filesystem::path absPath = std::filesystem::weakly_canonical(outPath, ec);
                const std::string rawPathStr = ec ? outPath.string() : absPath.string();

                drawCenteredText("Conversion réussie !", 85.0f, 22.0f, {22, 163, 74, 255});
                drawCenteredText("Fichier généré :", 122.0f, 16.0f, {100, 116, 139, 255});

                const Rectangle pathBox{64.0f, 150.0f, static_cast<float>(WINDOW_WIDTH - 128), 38.0f};
                DrawRectangleRounded(pathBox, 0.2f, 8, {241, 245, 249, 255});
                DrawRectangleRoundedLinesEx(pathBox, 0.2f, 8, 1.0f, {203, 213, 225, 255});

                const float pathFontSize = 16.0f;
                const std::string fittedPath = fitTextToWidth(rawPathStr, pathFontSize, pathBox.width - 24.0f, true);
                const Vector2 fittedSize = MeasureTextEx(m_font, fittedPath.c_str(), pathFontSize, TEXT_SPACING);
                DrawTextEx(
                    m_font,
                    fittedPath.c_str(),
                    {pathBox.x + (pathBox.width - fittedSize.x) / 2.0f, pathBox.y + (pathBox.height - fittedSize.y) / 2.0f},
                    pathFontSize,
                    TEXT_SPACING,
                    {30, 41, 59, 255}
                );
            } else if (status == ConversionStatus::MISSING_TOOL) {
                drawCenteredText("Outil CLI manquant", 115.0f, 22.0f, {220, 38, 38, 255});
                const std::string missingMsg = (m_detectedCategory == FileCategory::AUDIO || m_detectedCategory == FileCategory::VIDEO)
                    ? "Veuillez installer FFmpeg ('ffmpeg') dans le PATH."
                    : "Veuillez installer ImageMagick ('magick') dans le PATH.";
                drawCenteredText(missingMsg, 152.0f, 16.0f, {100, 116, 139, 255});
            } else {
                drawCenteredText("Erreur lors de la conversion", 115.0f, 22.0f, {220, 38, 38, 255});
                drawCenteredText("La commande CLI externe a échoué.", 152.0f, 16.0f, {100, 116, 139, 255});
            }

            drawCenteredText("Glissez un autre fichier ou cliquez ci-dessous :", 212.0f, 16.0f, {100, 116, 139, 255});

            const Rectangle newFileBtn{200.0f, 248.0f, 200.0f, 44.0f};
            if (drawButton(newFileBtn, "Nouveau fichier", {37, 99, 235, 255}, {29, 78, 216, 255}, RAYWHITE)) {
                resetState();
            }
            break;
        }
    }
}

void App::resetState() {
    m_converter.reset();
    m_currentFile.clear();
    m_detectedType = FileType::UNKNOWN;
    m_detectedCategory = FileCategory::UNKNOWN;
    m_availableTargets.clear();
    m_selectedTargetIndex = 0;
    m_dropdownOpen = false;
    m_state = AppState::WAITING_DROP;
}
