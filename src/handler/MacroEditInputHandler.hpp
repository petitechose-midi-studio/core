#pragma once

/**
 * @file MacroEditInputHandler.hpp
 * @brief Handles input for MacroEdit overlay
 *
 * Two-level scoping:
 * - macroViewScope_: Press macro button to open overlay
 * - overlayScope_: All other bindings when overlay is visible
 */

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"
#include "state/OverlayController.hpp"
#include "ui/overlay/MacroEditOverlay.hpp"

namespace handler {

/**
 * @brief Handles input for MacroEdit overlay
 *
 * When a macro button is pressed (from MacroView), opens the edit overlay.
 * When overlay is visible, NAV encoder adjusts CH/CC values.
 */
class MacroEditInputHandler {
public:
    /**
     * @brief Construct handler
     * @param state Core state reference
     * @param overlays Overlay controller for show/hide
     * @param overlay MacroEditOverlay for focus management
     * @param encoders Encoder API for NAV encoder
     * @param buttons Button API for macro buttons
     * @param macroViewScope Scope element for macro view (open trigger)
     * @param overlayScope Scope element for overlay (edit/close)
     */
    MacroEditInputHandler(
        state::CoreState& state,
        state::OverlayController& overlays,
        ui::MacroEditOverlay& overlay,
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

    state::CoreState& state_;
    state::OverlayController& overlays_;
    ui::MacroEditOverlay& overlay_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;

    lv_obj_t* macroViewScope_;
    lv_obj_t* overlayScope_;

    uint8_t focusedRow_ = 0;  ///< 0 = channel, 1 = CC
};

}  // namespace handler
