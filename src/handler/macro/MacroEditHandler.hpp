#pragma once

/**
 * @file MacroEditHandler.hpp
 * @brief Handles input for MacroEdit overlay
 *
 * Two-level scoping:
 * - macro_view_scope_: Press macro button to open overlay
 * - overlay_scope_: All other bindings when overlay is visible
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"
#include "state/OverlayManager.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

/**
 * @brief Handles input for MacroEdit overlay
 *
 * When a macro button is pressed (from MacroView), opens the edit overlay.
 * When overlay is visible, NAV encoder adjusts CH/CC values.
 * Updates state signals (focusedRow, tempChannel, tempCC) - overlay renders from state.
 */
class MacroEditHandler {
public:
    /**
     * @brief Construct handler
     * @param state Core state reference
     * @param overlays Overlay controller for show/hide
     * @param encoders Encoder API for NAV encoder
     * @param buttons Button API for macro buttons
     * @param macroViewScope Scope element for macro view (open trigger)
     * @param overlayScope Scope element for overlay (edit/close)
     */
    MacroEditHandler(
        core::state::CoreState& state,
        core::state::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* macroViewScope,
        lv_obj_t* overlayScope
    );

    ~MacroEditHandler() = default;

    // Non-copyable, non-movable
    MacroEditHandler(const MacroEditHandler&) = delete;
    MacroEditHandler& operator=(const MacroEditHandler&) = delete;
    MacroEditHandler(MacroEditHandler&&) = delete;
    MacroEditHandler& operator=(MacroEditHandler&&) = delete;

private:
    void setupBindings();

    void openEdit(uint8_t macroIndex);
    void closeWithoutSave();
    void saveAndClose();
    void adjustValue(float delta);
    void toggleFocus();

    core::state::CoreState& state_;
    core::state::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;

    lv_obj_t* macro_view_scope_;
    lv_obj_t* overlay_scope_;
};

}  // namespace core::handler
