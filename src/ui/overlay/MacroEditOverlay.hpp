#pragma once

/**
 * @file MacroEditOverlay.hpp
 * @brief Overlay for editing macro CH/CC configuration
 *
 * Layout:
 * ┌─────────────────────────┐
 * │  Edit Macro 1           │  Header
 * ├─────────────────────────┤
 * │  CH: [1-16]   ◄►        │  Channel row
 * │  CC: [0-127]  ◄►        │  CC row
 * └─────────────────────────┘
 */

#include <memory>
#include <vector>

#include <lvgl.h>
#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IComponent.hpp>
#include <oc/ui/lvgl/widget/Label.hpp>

#include "state/MacroEditState.hpp"

namespace ui {

/**
 * @brief Overlay for editing macro CH/CC configuration
 *
 * Shows a modal dialog to edit MIDI channel and CC number
 * for a macro slot. Uses NAV encoder to adjust values.
 */
class MacroEditOverlay : public oc::ui::lvgl::IComponent {
public:
    /**
     * @brief Construct overlay
     * @param parent Parent LVGL object (usually lv_screen_active())
     * @param state Reference to MacroEditState for reactive updates
     */
    MacroEditOverlay(lv_obj_t* parent, state::MacroEditState& state);
    ~MacroEditOverlay();

    // Non-copyable, non-movable
    MacroEditOverlay(const MacroEditOverlay&) = delete;
    MacroEditOverlay& operator=(const MacroEditOverlay&) = delete;
    MacroEditOverlay(MacroEditOverlay&&) = delete;
    MacroEditOverlay& operator=(MacroEditOverlay&&) = delete;

    // IComponent interface
    void show() override;
    void hide() override;
    bool isVisible() const override;
    lv_obj_t* getElement() const override { return overlay_; }

    /**
     * @brief Set focused row (for visual feedback)
     * @param row 0 = channel, 1 = CC
     */
    void setFocusedRow(uint8_t row);

private:
    void createLayout(lv_obj_t* parent);
    void bindToState();
    void updateFocusIndicator();

    state::MacroEditState& state_;

    // LVGL objects
    lv_obj_t* overlay_ = nullptr;       ///< Fullscreen semi-transparent background
    lv_obj_t* container_ = nullptr;     ///< Center dialog box
    lv_obj_t* chRow_ = nullptr;         ///< Channel row container
    lv_obj_t* ccRow_ = nullptr;         ///< CC row container

    // Labels
    std::unique_ptr<oc::ui::lvgl::Label> titleLabel_;
    std::unique_ptr<oc::ui::lvgl::Label> channelPrefixLabel_;
    std::unique_ptr<oc::ui::lvgl::Label> channelValueLabel_;
    std::unique_ptr<oc::ui::lvgl::Label> ccPrefixLabel_;
    std::unique_ptr<oc::ui::lvgl::Label> ccValueLabel_;

    // Focus state
    uint8_t focusedRow_ = 0;  ///< 0 = channel, 1 = CC

    // Subscriptions
    std::vector<oc::state::Subscription> subs_;
};

}  // namespace ui
