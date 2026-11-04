#include "App.hpp"
#include "FormatMatrix.hpp"

#include <array>
#include <optional>

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

} // namespace

App::App()
    : m_magickAvailable(m_converter.isMagickAvailable()) {}

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
        // Charge les caractères ASCII et Latin-1 (accents français inclus)
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

void App::run() {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Convertix - Convertisseur d'Images");
    SetTargetFPS(60);

    loadAppFont();

    while (!WindowShouldClose()) {
        update();

        BeginDrawing();
        render();
        EndDrawing();
    }

    unloadAppFont();
    CloseWindow();
}

void App::handleDroppedFiles() {
    if (!IsFileDropped()) {
        return;
    }

    FilePathList droppedFiles = LoadDroppedFiles();
    if (droppedFiles.count > 0 && m_state != AppState::CONVERTING) {
        m_converter.reset();
        m_currentFile = std::filesystem::path(droppedFiles.paths[0]);
        m_detectedType = FileDetector::detect(m_currentFile);
        m_availableTargets = FormatMatrix::getAvailableTargets(m_detectedType);
        m_selectedTargetIndex = 0;
        m_dropdownOpen = false;
        m_state = AppState::FILE_DETECTED;
    }
    UnloadDroppedFiles(droppedFiles);
}

void App::update() {
    handleDroppedFiles();

    if (m_state == AppState::CONVERTING) {
        const ConversionStatus status = m_converter.getStatus();
        if (status == ConversionStatus::SUCCESS || status == ConversionStatus::ERROR_CLI) {
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

    // En-tête
    DrawRectangle(0, 0, WINDOW_WIDTH, 54, {30, 41, 59, 255});
    drawCenteredText("Convertix - Convertisseur d'Images", 16.0f, 22.0f, RAYWHITE);

    if (!m_magickAvailable) {
        DrawRectangle(0, 54, WINDOW_WIDTH, 28, {254, 226, 226, 255});
        drawCenteredText("Attention : ImageMagick ('magick') introuvable dans le PATH", 59.0f, 16.0f, MAROON);
    }

    const Rectangle dropZone{40.0f, 85.0f, static_cast<float>(WINDOW_WIDTH - 80), 275.0f};
    DrawRectangleRounded(dropZone, 0.06f, 8, RAYWHITE);
    drawDashedRect(dropZone, 12.0f, 8.0f, 2.0f, {148, 163, 184, 255});

    switch (m_state) {
        case AppState::WAITING_DROP: {
            drawCenteredText("Glissez et déposez une image ici", 195.0f, 22.0f, {51, 65, 85, 255});
            drawCenteredText("Formats supportés : PNG, JPEG, WEBP, BMP, GIF", 232.0f, 16.0f, {100, 116, 139, 255});
            break;
        }

        case AppState::FILE_DETECTED: {
            const std::string rawFileName = m_currentFile.filename().string();
            const std::string displayFileName = fitTextToWidth("Fichier : " + rawFileName, 20.0f, dropZone.width - 40.0f, false);
            drawCenteredText(displayFileName, 108.0f, 20.0f, {30, 41, 59, 255});

            if (m_detectedType == FileType::UNKNOWN) {
                drawCenteredText("Format non supporté", 165.0f, 22.0f, {220, 38, 38, 255});
                drawCenteredText("Signature binaire (Magic Bytes) non reconnue.", 198.0f, 16.0f, {100, 116, 139, 255});

                const Rectangle resetBtn{200.0f, 260.0f, 200.0f, 42.0f};
                if (drawButton(resetBtn, "Réinitialiser", {71, 85, 105, 255}, {51, 65, 85, 255}, RAYWHITE)) {
                    resetState();
                }
            } else {
                const std::string detectedStr = "Format réel détecté : " + FileDetector::toString(m_detectedType);
                drawCenteredText(detectedStr, 142.0f, 18.0f, {22, 163, 74, 255});
                drawCenteredText("Sélectionnez le format cible :", 178.0f, 16.0f, {100, 116, 139, 255});

                // Zone Sélecteur (Dropdown) + Bouton [ Convertir ] côte à côte
                const Rectangle dropdownBox{125.0f, 210.0f, 185.0f, 42.0f};
                const Rectangle convertBtn{325.0f, 210.0f, 150.0f, 42.0f};
                const Rectangle cancelBtn{220.0f, 298.0f, 160.0f, 36.0f};

                // Les boutons sous-jacents sont désactivés quand le menu déroulant est ouvert pour éviter tout conflit de clic
                if (drawButton(convertBtn, "Convertir", {37, 99, 235, 255}, {29, 78, 216, 255}, RAYWHITE, !m_dropdownOpen)) {
                    if (!m_availableTargets.empty() && m_selectedTargetIndex < m_availableTargets.size()) {
                        m_converter.startConversion(m_currentFile, m_availableTargets[m_selectedTargetIndex]);
                        m_dropdownOpen = false;
                        m_state = AppState::CONVERTING;
                        break;
                    }
                }

                if (drawButton(cancelBtn, "Réinitialiser", {203, 213, 225, 255}, {148, 163, 184, 255}, {30, 41, 59, 255}, !m_dropdownOpen)) {
                    resetState();
                    break;
                }

                // Rendu du bouton principal de la liste déroulante
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

                // Petite flèche d'ouverture/fermeture à droite du sélecteur
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

                // Liste déroulante dépliée (dessinée en dernier pour passer au-dessus des autres éléments)
                bool clickedInsideDropdown = false;
                if (m_dropdownOpen && !m_availableTargets.empty()) {
                    const float itemHeight = 36.0f;
                    const float listHeight = static_cast<float>(m_availableTargets.size()) * itemHeight;
                    const Rectangle listBounds{dropdownBox.x, dropdownBox.y + dropdownBox.height + 4.0f, dropdownBox.width, listHeight};

                    // Ombre légère et fond de la liste
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

                // Gestion de l'ouverture/fermeture au clic
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
            DrawRing({static_cast<float>(WINDOW_WIDTH) / 2.0f, 185.0f}, 18.0f, 24.0f, startAngle, startAngle + 270.0f, 32, {37, 99, 235, 255});

            drawCenteredText("Conversion en cours...", 235.0f, 22.0f, {30, 41, 59, 255});
            drawCenteredText("Traitement asynchrone via ImageMagick", 268.0f, 16.0f, {100, 116, 139, 255});
            break;
        }

        case AppState::FINISHED: {
            if (m_converter.getStatus() == ConversionStatus::SUCCESS) {
                const std::filesystem::path outPath = m_converter.getLastOutputPath();
                std::error_code ec;
                const std::filesystem::path absPath = std::filesystem::weakly_canonical(outPath, ec);
                const std::string rawPathStr = ec ? outPath.string() : absPath.string();

                drawCenteredText("Conversion terminée avec succès !", 118.0f, 22.0f, {22, 163, 74, 255});
                drawCenteredText("Fichier généré :", 154.0f, 16.0f, {100, 116, 139, 255});

                // Encadré dédié pour afficher le chemin de sortie (tronqué proprement si trop long)
                const Rectangle pathBox{64.0f, 180.0f, static_cast<float>(WINDOW_WIDTH - 128), 38.0f};
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
            } else {
                drawCenteredText("Erreur lors de la conversion", 145.0f, 22.0f, {220, 38, 38, 255});
                drawCenteredText("Vérifiez qu'ImageMagick ('magick') est installé.", 182.0f, 16.0f, {100, 116, 139, 255});
            }

            drawCenteredText("Glissez un autre fichier ou cliquez ci-dessous :", 238.0f, 16.0f, {100, 116, 139, 255});

            const Rectangle newFileBtn{200.0f, 272.0f, 200.0f, 44.0f};
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
    m_availableTargets.clear();
    m_selectedTargetIndex = 0;
    m_dropdownOpen = false;
    m_state = AppState::WAITING_DROP;
}
