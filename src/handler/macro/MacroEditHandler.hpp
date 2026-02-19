#pragma once

/**
 * @file MacroEditHandler.hpp
 * @brief Handles input for MacroEdit overlay
 *
 * Input scopes:
 * - macro_view_scope_: Long press macro opens overlay
 * - overlay_scope_: Main property overlay bindings
 * - selector_scope_: Value selector bindings
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

/**
 * @brief Handles input for MacroEdit overlay
 *
 * - Long press on macro button opens MacroEdit for that macro
 * - Main overlay: NAV turn (focus row), OPT turn (live value edit), NAV press (open value selector)
 * - Value selector: NAV turn (navigate), NAV release (apply and close)
 * - LEFT_TOP closes overlay (no rollback)
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
     * @param overlayScope Scope element for main MacroEdit overlay
     * @param selectorScope Scope element for MacroEdit value selector overlay
     * @param pageSelectorScope Scope element for page selector overlay
     * @param macroSelectorScope Scope element for macro selector overlay
     */
    MacroEditHandler(
        core::state::CoreState& state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* macroViewScope,
        lv_obj_t* overlayScope,
        lv_obj_t* selectorScope,
        lv_obj_t* pageSelectorScope,
        lv_obj_t* macroSelectorScope
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
    void handleOpeningMacroRelease(uint8_t macroIndex);
    void closeOverlay();

    void moveFocus(float delta);
    void setFocusedValue(float normalized);
    void openValueSelector();
    void navigateValueSelector(float delta);
    void applyValueSelectorAndClose();

    void openPageSelector();
    void navigatePageSelector(float delta);
    void applyPageSelectorAndClose();

    void openMacroTargetSelector();
    void navigateMacroTargetSelector(float delta);
    void applyMacroTargetSelectorAndClose();

    void setValueForRow(uint8_t row, int value);
    int valueForRow(uint8_t row) const;
    int valueCountForRow(uint8_t row) const;
    void applyTempConfig();
    void configureOptForFocusedRow();

    core::state::CoreState& state_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;

    lv_obj_t* macro_view_scope_;
    lv_obj_t* overlay_scope_;
    lv_obj_t* selector_scope_;
    lv_obj_t* page_selector_scope_;
    lv_obj_t* macro_selector_scope_;

    bool has_staged_config_changes_ = false;
};

}  // namespace core::handler
