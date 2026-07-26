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

#include "handler/common/ProjectRecordedShapeCaptureWorkflow.hpp"
#include "handler/macro/MacroEditDomainServices.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/MacroEditState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroEditMenuModel.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class MacroMidiCcRuntimeAdapter;

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
        core::state::StatusBarState& statusBar;
        core::state::macro::MacroHistoryService& history;
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
     */
    MacroEditHandler(
        StateRefs state,
        MacroEditDomainServices services,
        MacroPerformanceDomainServices performanceServices,
        MacroMidiCcRuntimeAdapter& midiRuntime,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID macroViewScope,
        oc::type::ScopeID overlayScope,
        oc::type::ScopeID selectorScope,
        NowProvider nowProvider
    );

    ~MacroEditHandler() = default;

    // Non-copyable, non-movable
    MacroEditHandler(const MacroEditHandler&) = delete;
    MacroEditHandler& operator=(const MacroEditHandler&) = delete;
    MacroEditHandler(MacroEditHandler&&) = delete;
    MacroEditHandler& operator=(MacroEditHandler&&) = delete;

    void update(uint32_t nowMs);
    void openFocusedMacro(uint8_t macroIndex) { openEdit(macroIndex); }

private:
    void setupBindings();

    void openEdit(uint8_t macroIndex);
    void closeOverlay();

    void moveFocus(float delta);
    void setFocusedValue(float normalized);
    void openValueSelector();
    void navigateValueSelector(float delta);
    void applyValueSelectorAndClose();

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

    void setValueForRow(core::state::macro::MacroRootItem item, int value);
    int valueForRow(core::state::macro::MacroRootItem item) const;
    int valueCountForRow(core::state::macro::MacroRootItem item) const;
    void commitEditedConfig();
    void configureOptForFocusedRow();
    void copyFocusedDomain();
    void beginBottomRightAction();
    void releaseBottomRightAction();
    void beginBottomLeftAction();
    void releaseBottomLeftAction();
    void commitGuardedAction(uint32_t nowMs);
    void publishRecordedShapeCaptureRevision();

    core::state::MacroEditState& macro_edit_;
    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    MacroEditDomainServices services_;
    MacroPerformanceDomainServices performance_services_;
    ProjectRecordedShapeCaptureWorkflow recorded_shape_capture_;
    MacroMidiCcRuntimeAdapter& midi_runtime_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;

    oc::type::ScopeID macro_view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
    oc::type::ScopeID selector_scope_ = 0;
    NowProvider now_provider_ = nullptr;
    bool context_record_active_ = false;
    bool context_recorded_shape_active_ = false;
    bool track_channel_gesture_active_ = false;
};

}  // namespace core::handler
