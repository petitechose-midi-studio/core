#pragma once

#include <cstdint>
#include <algorithm>

namespace desktop {

/**
 * @brief Display mode for the simulator
 */
enum class DisplayMode {
    RealSize,  // 1:1 physical size (19cm)
    Fit        // Scale to fit window (aspect ratio preserved)
};

/**
 * @brief Hardware layout configuration for HwSimulator
 *
 * Supports DPI-aware rendering for real-world size display.
 * Physical controller size: 19cm x 19cm
 */
struct HwLayout {
    // ════════════════════════════════════════════════════════════
    // Physical constants (mm)
    // ════════════════════════════════════════════════════════════
    static constexpr float PANEL_SIZE_MM = 190.0f;  // 19cm
    static constexpr float SCREEN_WIDTH_MM = 60.0f;
    static constexpr float SCREEN_HEIGHT_MM = 45.0f;

    // ════════════════════════════════════════════════════════════
    // Screen (LVGL app area)
    // ════════════════════════════════════════════════════════════
    static constexpr int LOGICAL_SCREEN_W = 320;  // Virtual resolution
    static constexpr int LOGICAL_SCREEN_H = 240;

    int screenW = 320;   // Physical size (scaled)
    int screenH = 240;
    int screenX = 0;
    int screenY = 0;
    float screenScale = 1.0f;  // screenW / LOGICAL_SCREEN_W

    // ════════════════════════════════════════════════════════════
    // Panel (controller)
    // ════════════════════════════════════════════════════════════
    int panelSize = 1013;  // Default at 96 DPI
    int panelRadius = 20;  // Rounded corners

    // ════════════════════════════════════════════════════════════
    // DPI and scaling
    // ════════════════════════════════════════════════════════════
    float dpi = 96.0f;
    float scale = 1.0f;  // For Fit mode

    // ════════════════════════════════════════════════════════════
    // Control sizes (radii in pixels)
    // ════════════════════════════════════════════════════════════
    int btnRadius = 32;
    int navRadius = 21;
    int optRadius = 68;
    int macroRadius = 39;

    // ════════════════════════════════════════════════════════════
    // Left buttons positions
    // ════════════════════════════════════════════════════════════
    int leftBtnX = 0;
    int leftBtnYTop = 0;
    int leftBtnYCenter = 0;
    int leftBtnYBottom = 0;

    // ════════════════════════════════════════════════════════════
    // Bottom buttons positions
    // ════════════════════════════════════════════════════════════
    int bottomBtnY = 0;
    int bottomBtnXLeft = 0;
    int bottomBtnXCenter = 0;
    int bottomBtnXRight = 0;

    // ════════════════════════════════════════════════════════════
    // Right controls (NAV, OPT)
    // ════════════════════════════════════════════════════════════
    int rightX = 0;
    int navY = 0;
    int optY = 0;

    // ════════════════════════════════════════════════════════════
    // Macro grid (4x2)
    // ════════════════════════════════════════════════════════════
    int macroStartX = 0;
    int macroStartY = 0;
    int macroSpacingX = 0;
    int macroSpacingY = 0;

    // ════════════════════════════════════════════════════════════
    // Factory methods
    // ════════════════════════════════════════════════════════════

    /**
     * @brief Create layout for real-size display (1:1)
     * @param dpi Display DPI (use SDL_GetDisplayDPI)
     */
    static HwLayout realSize(float dpi) {
        HwLayout layout;
        layout.dpi = dpi;
        layout.scale = 1.0f;

        // Calculate panel size in pixels for 19cm
        float dotsPerMm = dpi / 25.4f;
        layout.panelSize = static_cast<int>(PANEL_SIZE_MM * dotsPerMm);

        layout.computePositions();
        return layout;
    }

    /**
     * @brief Create layout scaled to fit window
     * @param windowSize Minimum of window width/height
     * @param padding Padding around panel
     */
    static HwLayout fit(int windowSize, int padding = 20) {
        HwLayout layout;
        layout.panelSize = windowSize - padding * 2;
        layout.scale = static_cast<float>(layout.panelSize) / 1013.0f;  // Base size

        layout.computePositions();
        return layout;
    }

    /**
     * @brief Default layout (96 DPI reference)
     */
    static HwLayout midiStudio() {
        return realSize(96.0f);
    }

    /**
     * @brief Recompute all positions based on panelSize
     */
    void computePositions() {
        float pxPerMm = panelSize / PANEL_SIZE_MM;

        // Screen is ALWAYS 320x240 pixels (non-negotiable)
        screenW = LOGICAL_SCREEN_W;  // 320
        screenH = LOGICAL_SCREEN_H;  // 240

        // Visual size of screen area in the panel (for positioning)
        int visualScreenW = static_cast<int>(SCREEN_WIDTH_MM * pxPerMm);
        int visualScreenH = static_cast<int>(SCREEN_HEIGHT_MM * pxPerMm);
        screenScale = static_cast<float>(visualScreenW) / LOGICAL_SCREEN_W;

        // Screen position (centered horizontally, fixed ratio from top)
        // Use visual size for centering
        screenX = (panelSize - visualScreenW) / 2;
        screenY = static_cast<int>(panelSize * 0.079f);

        // Control radii (from mm)
        btnRadius = static_cast<int>(6.0f * pxPerMm);
        navRadius = static_cast<int>(4.0f * pxPerMm);
        optRadius = static_cast<int>(12.75f * pxPerMm);
        macroRadius = static_cast<int>(7.3f * pxPerMm);

        // Panel rounded corners
        panelRadius = static_cast<int>(5.0f * pxPerMm);

        // Left buttons
        leftBtnX = screenX - static_cast<int>(30.0f * pxPerMm);
        leftBtnYTop = screenY + btnRadius;
        leftBtnYCenter = screenY + screenH / 2;
        leftBtnYBottom = screenY + screenH - btnRadius;

        // Bottom buttons
        bottomBtnY = static_cast<int>(panelSize * 0.395f);
        bottomBtnXLeft = screenX + btnRadius;
        bottomBtnXCenter = screenX + screenW / 2;
        bottomBtnXRight = screenX + screenW - btnRadius;

        // Right controls
        rightX = screenX + screenW + static_cast<int>(18.8f * pxPerMm) + optRadius;
        navY = screenY + navRadius;
        optY = screenY + screenH - optRadius;

        // Macro grid
        macroSpacingX = static_cast<int>(panelSize * 0.219f);
        macroSpacingY = static_cast<int>(panelSize * 0.197f);
        macroStartX = (panelSize - 3 * macroSpacingX) / 2;
        macroStartY = static_cast<int>(panelSize * 0.612f);
    }

    /**
     * @brief Update layout for new window size (Fit mode)
     */
    void updateForWindowSize(int windowW, int windowH, int padding = 20) {
        int minDim = std::min(windowW, windowH);
        panelSize = minDim - padding * 2;
        scale = static_cast<float>(panelSize) / 1013.0f;
        computePositions();
    }

    /**
     * @brief Get panel offset to center in window
     */
    int getPanelOffsetX(int windowW) const {
        return (windowW - panelSize) / 2;
    }

    int getPanelOffsetY(int windowH) const {
        return (windowH - panelSize) / 2;
    }
};

} // namespace desktop
