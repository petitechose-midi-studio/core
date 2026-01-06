#pragma once

/**
 * @file HandlerInputMacroEdit.hpp
 * @brief Handles input for MacroEdit overlay
 *
 * Two-level scoping:
 * - macroViewScope_: Press macro button to open overlay
 * - overlayScope_: All other bindings when overlay is visible
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayController.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

/**
 * @brief Handles input for MacroEdit overlay
 *
 * When a macro button is pressed (from MacroView), opens the edit overlay.
 * When overlay is visible, NAV encoder adjusts CH/CC values.
 * Updates state signals (focusedRow, tempChannel, tempCC) - overlay renders from state.
 */
class HandlerInputMacroEdit {
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
    HandlerInputMacroEdit(
        core::state::CoreState& state,
        core::ui::OverlayController<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* macroViewScope,
        lv_obj_t* overlayScope
    );

private:
    void setupBindings();

    void openEdit(uint8_t macroIndex);
    void closeWithoutSave();
    void saveAndClose();
    void adjustValue(float delta);
    void toggleFocus();

    core::state::CoreState& state_;
    core::ui::OverlayController<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;

    lv_obj_t* macroViewScope_;
    lv_obj_t* overlayScope_;
};

}  // namespace core::handler
