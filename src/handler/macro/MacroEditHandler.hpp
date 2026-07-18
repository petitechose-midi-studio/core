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

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "handler/macro/MacroEditDomainServices.hpp"
#include "state/MacroEditState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

/**
 * @brief Handles input for MacroEdit overlay
 *
 * - Long press on macro button opens MacroEdit for that macro
 * - Main overlay: NAV turn (focus row), OPT turn (overlay-local value edit), NAV press (open value selector)
 * - Value selector: NAV turn (navigate), NAV release (apply and close)
 * - LEFT_TOP closes overlay and commits the buffered edit
 */
class MacroEditHandler {
public:
    using NowProvider = uint32_t (*)();

    struct StateRefs {
        core::state::MacroEditState& macroEdit;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
    };

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
        StateRefs state,
        MacroEditDomainServices services,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID macroViewScope,
        oc::type::ScopeID overlayScope,
        oc::type::ScopeID selectorScope,
        oc::type::ScopeID pageSelectorScope,
        oc::type::ScopeID macroSelectorScope,
        NowProvider nowProvider
    );

    ~MacroEditHandler() = default;

    // Non-copyable, non-movable
    MacroEditHandler(const MacroEditHandler&) = delete;
    MacroEditHandler& operator=(const MacroEditHandler&) = delete;
    MacroEditHandler(MacroEditHandler&&) = delete;
    MacroEditHandler& operator=(MacroEditHandler&&) = delete;

    void update(uint32_t nowMs);

private:
    void setupBindings();

    void openEdit(uint8_t macroIndex);
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

    void beginContextSelector();
    void endContextSelector();
    void navigateContextProperty(float delta);
    int contextPropertyCount() const;
    int contextValueCount() const;
    int contextValue() const;
    void setContextValue(float normalized);
    void beginMacroCycle();
    void endMacroCycle();
    void cycleActiveMacro(float delta);

    void setValueForRow(uint8_t row, int value);
    int valueForRow(uint8_t row) const;
    int valueCountForRow(uint8_t row) const;
    void commitEditedConfig();
    void configureOptForFocusedRow();
    void copyFocusedDomain();
    void beginBottomRightAction();
    void releaseBottomRightAction();
    void beginBottomLeftAction();
    void releaseBottomLeftAction();
    void commitGuardedAction(uint32_t nowMs);

    core::state::MacroEditState& macro_edit_;
    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    MacroEditDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;

    oc::type::ScopeID macro_view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
    oc::type::ScopeID selector_scope_ = 0;
    oc::type::ScopeID page_selector_scope_ = 0;
    oc::type::ScopeID macro_selector_scope_ = 0;
    NowProvider now_provider_ = nullptr;
    bool edit_entry_chord_active_ = false;
};

}  // namespace core::handler
