#pragma once

/**
 * @file MacroEditOverlay.hpp
 * @brief Stateless overlay for editing macro CH/CC configuration
 *
 * Uses Props pattern (like plugin-bitwig overlays):
 * - Component is stateless
 * - render(props) called by orchestrator (Context/View)
 * - No internal subscriptions
 *
 * Layout:
 * ┌─────────────────────────┐
 * │  Edit Macro 1           │  Header
 * ├─────────────────────────┤
 * │  CH: [1-16]   ◄►        │  Channel row
 * │  CC: [0-127]  ◄►        │  CC row
 * └─────────────────────────┘
 */

#include <cstdint>
#include <memory>

#include <lvgl.h>
#include <oc/ui/lvgl/widget/Label.hpp>

namespace core::ui {

/**
 * @brief Props for MacroEditOverlay rendering
 */
struct MacroEditOverlayProps {
    uint8_t editingIndex = 0;   ///< Which macro (0-7)
    uint8_t channel = 0;        ///< MIDI channel (0-15, displayed as 1-16)
    uint8_t cc = 0;             ///< CC number (0-127)
    uint8_t focusedRow = 0;     ///< 0 = channel, 1 = CC
    bool visible = false;       ///< Overlay visibility

    bool operator==(const MacroEditOverlayProps& other) const {
        return editingIndex == other.editingIndex
            && channel == other.channel
            && cc == other.cc
            && focusedRow == other.focusedRow
            && visible == other.visible;
    }

    bool operator!=(const MacroEditOverlayProps& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Stateless overlay for editing macro CH/CC configuration
 *
 * Shows a modal dialog to edit MIDI channel and CC number
 * for a macro slot. Call render(props) to update display.
 */
class MacroEditOverlay {
public:
    /**
     * @brief Construct overlay (stateless - no state reference)
     * @param parent Parent LVGL object (usually lv_screen_active())
     */
    explicit MacroEditOverlay(lv_obj_t* parent);
    ~MacroEditOverlay();

    // Non-copyable, non-movable
    MacroEditOverlay(const MacroEditOverlay&) = delete;
    MacroEditOverlay& operator=(const MacroEditOverlay&) = delete;
    MacroEditOverlay(MacroEditOverlay&&) = delete;
    MacroEditOverlay& operator=(MacroEditOverlay&&) = delete;

    /**
     * @brief Render overlay with given props
     *
     * Pure rendering function - transforms props to UI.
     * Optimized: skips updates when props unchanged.
     *
     * @param props Current state to render
     */
    void render(const MacroEditOverlayProps& props);

    /**
     * @brief Get LVGL element for scoping
     */
    lv_obj_t* getElement() const { return overlay_; }

private:
    void createLayout(lv_obj_t* parent);
    void updateFocusIndicator(uint8_t focusedRow);

    // LVGL objects
    lv_obj_t* overlay_ = nullptr;       ///< Fullscreen semi-transparent background
    lv_obj_t* container_ = nullptr;     ///< Center dialog box
    lv_obj_t* ch_row_ = nullptr;         ///< Channel row container
    lv_obj_t* cc_row_ = nullptr;         ///< CC row container

    // Labels
    std::unique_ptr<oc::ui::lvgl::Label> title_label_;
    std::unique_ptr<oc::ui::lvgl::Label> channel_prefix_label_;
    std::unique_ptr<oc::ui::lvgl::Label> channel_value_label_;
    std::unique_ptr<oc::ui::lvgl::Label> cc_prefix_label_;
    std::unique_ptr<oc::ui::lvgl::Label> cc_value_label_;

    // Cache for optimization
    MacroEditOverlayProps current_props_;
};

}  // namespace core::ui
