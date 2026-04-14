#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "handler/macro/MacroDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class MacroPerformanceHandler {
public:
    struct StateRefs {
        core::state::macro::MacroUiState& macroUi;
        core::state::macro::MacroPagesState& pages;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::StructureClipboardState& structureClipboard;
    };

    MacroPerformanceHandler(StateRefs state,
                            MacroDomainServices services,
                            oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                            oc::api::EncoderAPI& encoders,
                            oc::api::ButtonAPI& buttons,
                            oc::type::ScopeID scopeId);

    ~MacroPerformanceHandler() = default;

    MacroPerformanceHandler(const MacroPerformanceHandler&) = delete;
    MacroPerformanceHandler& operator=(const MacroPerformanceHandler&) = delete;

private:
    void bindStateSync();
    void setupBindings();
    void activateClutch();
    void deactivateClutch();
    void openQuickControls();
    void closeQuickControlsApply();
    void closeQuickControlsCancel();
    void navigateQuickControls(float delta);
    void setFocusedQuickControlValue(float normalized);
    void navigateProperty(float delta);
    bool commitPreviewedPageIfNeeded();
    void cycleNavigationFocus();
    void movePage(float delta);
    void moveTrack(float delta);
    void eraseCurrentStructure();
    void removeCurrentStructure();
    void copyCurrentStructure();
    void pasteCurrentStructure();
    void createPreviewedStructure();
    bool canRemoveCurrentStructure() const;
    bool canPasteCurrentStructure() const;
    void beginHoldAction(core::state::StructureHoldAction action);
    void clearHoldAction();
    void enterSelectionMode(core::state::StructureSelectionScope scope);
    void cancelSelectionMode();
    void toggleSelectionAtCursor();
    void navigateSelection(float delta);
    void deleteSelection();
    void duplicateSelection();
    void configureMacroEncoders();
    void configureValueEncoders();
    void configureDiscreteEncoders(uint8_t discreteSteps);
    void configureQuickControlEncoder();
    void resetQuickControlsState();
    void configureNormalizedEncoder(Config::EncoderID id);
    void configureDiscreteEncoder(Config::EncoderID id, uint8_t discreteSteps);
    int currentCcOffsetMin() const;
    int currentCcOffsetMax() const;
    float offsetToNormalized(int offset) const;
    int normalizedToOffset(float normalized) const;
    void initializeClutchChannelPreview();
    void commitClutchChannelPreview();

    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    core::state::TrackNavigationState& track_ui_;
    oc::state::Signal<uint8_t, 8>& shared_track_active_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::StructureClipboardState& structure_clipboard_;
    MacroDomainServices services_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    std::vector<oc::state::Subscription> subscriptions_;
    oc::type::ScopeID scope_id_ = 0;
    bool nav_long_press_used_ = false;
    bool left_center_held_ = false;
    bool left_bottom_held_ = false;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
    uint8_t quick_snapshot_page_ = 0;
    std::array<core::state::macro::MacroConfig, Config::MACRO_COUNT> quick_snapshot_configs_{};
};

}  // namespace core::handler
