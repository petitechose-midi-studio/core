#pragma once

#include <lvgl.h>
#include <vector>
#include <functional>
#include "HwLayout.hpp"
#include <config/InputIDs.hpp>

namespace oc::hal::desktop { class InputMapper; }

namespace desktop {

// Colors (matching hardware appearance)
namespace HwColor {
    constexpr uint32_t BACKGROUND = 0x3D2B1F;      // Wood brown
    constexpr uint32_t PANEL_BORDER = 0x2A1A10;
    constexpr uint32_t SCREEN_BG = 0x1A1A1A;

    constexpr uint32_t LEFT_TOP = 0xE53935;
    constexpr uint32_t LEFT_CENTER = 0xEF9A9A;
    constexpr uint32_t LEFT_BOTTOM = 0xFFCDD2;

    constexpr uint32_t BOTTOM_LEFT = 0x43A047;
    constexpr uint32_t BOTTOM_CENTER = 0x81C784;
    constexpr uint32_t BOTTOM_RIGHT = 0xC8E6C9;

    // Encoders - gray with subtle tints
    constexpr uint32_t ENCODER_GRAY = 0x606060;
    constexpr uint32_t NAV_GRAY = 0x506878;
    constexpr uint32_t OPT_GRAY = 0x786050;
}

/**
 * @brief LVGL-based hardware simulator
 *
 * Creates the visual representation of the hardware controller using LVGL widgets.
 * The panel is centered in the window and can resize with the window.
 *
 * Architecture:
 * - panel_: Main container with rounded corners (wood background)
 * - screenArea_: Container for app UI (320x240)
 * - buttons_: LVGL btn widgets
 * - encoderContainers_: Containers with arc + center circle
 */
class HwSimulator {
public:
    explicit HwSimulator(lv_obj_t* parent = nullptr);
    ~HwSimulator();

    // Non-copyable
    HwSimulator(const HwSimulator&) = delete;
    HwSimulator& operator=(const HwSimulator&) = delete;

    /**
     * @brief Initialize with layout
     */
    void init(const HwLayout& layout);

    /**
     * @brief Update layout (call on window resize)
     */
    void updateLayout(const HwLayout& layout);

    /**
     * @brief Get the screen area container for app UI
     */
    lv_obj_t* getScreenArea() const { return screenArea_; }

    /**
     * @brief Get the main panel object
     */
    lv_obj_t* getPanel() const { return panel_; }

    // ════════════════════════════════════════════════════════════
    // Input injection (connects LVGL widgets to app)
    // ════════════════════════════════════════════════════════════

    /**
     * @brief Connect to InputMapper for event injection
     * When LVGL widgets are clicked, events are sent to the app via InputMapper::post()
     */
    void setInputMapper(oc::hal::desktop::InputMapper* input) { inputMapper_ = input; }

    // ════════════════════════════════════════════════════════════
    // Visual feedback (from InputMapper)
    // ════════════════════════════════════════════════════════════

    void setButtonPressed(oc::hal::ButtonID id, bool pressed);
    void setEncoderValue(oc::hal::EncoderID id, float value);

    /**
     * @brief Handle mouse wheel over encoders
     * @param mouseX, mouseY Mouse position in LVGL coordinates
     * @param delta Wheel delta (positive = up, negative = down)
     * @return true if an encoder was under the cursor
     */
    bool handleMouseWheel(int mouseX, int mouseY, int delta);

private:
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* screenArea_ = nullptr;

    HwLayout layout_;

    // Button widgets
    struct ButtonWidget {
        Config::ButtonID id;
        lv_obj_t* obj;
        uint32_t color;
    };
    std::vector<ButtonWidget> buttons_;

    // Encoder widgets
    struct EncoderWidget {
        Config::EncoderID encId;
        Config::ButtonID btnId;  // ButtonID{0} if no button
        lv_obj_t* container;
        lv_obj_t* arc;
        lv_obj_t* dragZone;      // For drag rotation
        lv_obj_t* centerBtn;     // For button press (if any)
        uint32_t color;
        float value = 0.5f;
        int dragStartY = 0;      // Drag tracking
        bool dragging = false;
    };
    std::vector<EncoderWidget> encoders_;

    oc::hal::desktop::InputMapper* inputMapper_ = nullptr;

    // Creation helpers
    void createPanel();
    void createScreenArea();
    void createButtons();
    void createEncoders();

    void createButton(Config::ButtonID id, int x, int y, int radius, uint32_t color);
    void createEncoder(Config::EncoderID encId, Config::ButtonID btnId, int x, int y, int radius, uint32_t color);

    // Update helpers
    void updatePositions();

    // Event handlers
    static void buttonEventCb(lv_event_t* e);
    static void encoderEventCb(lv_event_t* e);

    // Color conversion
    static lv_color_t toLvColor(uint32_t rgb) {
        return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
};

} // namespace desktop
