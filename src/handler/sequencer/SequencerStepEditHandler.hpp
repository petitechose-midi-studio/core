#pragma once

/**
 * @file SequencerStepEditHandler.hpp
 * @brief Input bindings for the sequencer STEP EDIT overlay
 */

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "config/TimeCompat.hpp"
#include "handler/common/ButtonReleaseLatch.hpp"
#include "handler/sequencer/SequencerChordPresetDomainServices.hpp"
#include "handler/sequencer/SequencerChordPresetLibraryAdapter.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerPatternPresetDomainServices.hpp"
#include "handler/sequencer/SequencerPatternPresetLibraryAdapter.hpp"
#include "handler/sequencer/SequencerPresetLibraryWorkflow.hpp"
#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"
#include "handler/sequencer/SequencerStepPresetLibraryAdapter.hpp"
#include "state/PatternPitchSettingsState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

class SequencerStepEditHandler {
public:
    using TimeProviderFn = uint32_t (*)();

    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        core::state::StructureClipboardState& structureClipboard;
        core::state::TrackNavigationState& trackNavigation;
        core::state::PatternPitchSettingsState& patternPitchSettings;
        oc::state::Signal<core::state::StructureNavigationFocus,
                          core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        SequencerHistoryDomainServices history;
        SequencerStepPresetDomainServices stepPresets;
        SequencerChordPresetDomainServices chordPresets;
        SequencerPatternPresetDomainServices patternPresets;
    };

    SequencerStepEditHandler(StateRefs state,
                             oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                             oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
                             oc::type::ScopeID sequencerViewScope, oc::type::ScopeID overlayScope,
                             oc::type::ScopeID presetLibraryOverlayScope,
                             TimeProviderFn timeProvider = core::time_compat::millis);

    // Non-copyable, non-movable
    SequencerStepEditHandler(const SequencerStepEditHandler&) = delete;
    SequencerStepEditHandler& operator=(const SequencerStepEditHandler&) = delete;
    SequencerStepEditHandler(SequencerStepEditHandler&&) = delete;
    SequencerStepEditHandler& operator=(SequencerStepEditHandler&&) = delete;

    void update(uint32_t nowMs);

    /** Open the existing Step Editor on the focused Step and semantic row. */
    bool openFocusedStepAtRow(uint8_t row);
    /**
     * Open Chord detail or dive directly into a Micro/Cycle child from the
     * focused Step, without leaving the generic Step Editor in-between.
     */
    bool openFocusedStepContentAtRow(uint8_t row);
    /** Open the shared library for the active Pattern Editor target. */
    void openPatternPresetLibrary();
private:
    void setupBindings();

    void openForMacroInPage(uint8_t indexInPage);
    [[nodiscard]] bool drumStepEditActive() const;
    [[nodiscard]] bool drumChildEditActive() const;
    bool openDrumStepEditor(uint8_t lane, uint8_t step, uint8_t row);
    void closeDrumStepEditor();
    /** Resolve the Drum lane owner, then enter the shared Micro/Cycle graph. */
    bool openDrumOwnedSharedContentChild();
    [[nodiscard]] bool drumEditedStepInRange(uint8_t& step) const;
    [[nodiscard]] bool resolveDrumRootNodeId(
        core::state::sequencer::SequencerGraphNodeId& nodeId
    ) const;
    bool ensureDrumRootNodeId(
        core::state::sequencer::SequencerGraphNodeId& nodeId,
        bool& mappingChanged
    );
    void publishDrumAdvancedMutation(bool drumMappingChanged);
    [[nodiscard]] bool drumFocusedContextHasChild() const;
    [[nodiscard]] bool drumCanPasteFocusedContext() const;
    [[nodiscard]] core::state::sequencer::SequencerHistoryDescriptor
    drumStepHistoryDescriptor(
        core::state::sequencer::SequencerHistoryActionKind kind
    ) const;
    [[nodiscard]] int32_t drumStepHistoryValue() const;
    bool beginDrumStepHistory(
        core::state::sequencer::SequencerHistoryDescriptor descriptor
    );
    bool sealDrumStepHistory(
        bool changed,
        core::state::sequencer::SequencerHistoryDescriptor descriptor,
        bool commit
    );
    bool commitDrumStepHistory();
    void syncDrumPropertyForFocusedRow();
    void setDrumFocusedValue(float normalized);
    void configureDrumOpt();
    void resetDrumFocusedValue();
    void backFromStepEdit();
    bool commitStepEditHistory();
    void closeStepEdit();

    void moveFocus(float delta);
    void retargetEditedStep(float delta);
    void retargetEditedDrumLane(float delta);
    void activateFocusedRowOrClose();
    void setFocusedValue(float normalized);
    void configureOptForFocusedRow();
    void openChordEditor();
    void closeChordEditor();
    void applyStepContentDraft();
    void confirmStepContentDraftExitChoice();
    void moveChordEditorFocus(float delta);
    void setFocusedChordFieldValue(float normalized);
    void configureOptForFocusedChordField();
    void resetFocusedChordFieldToDefault();
    void toggleChordSourceSelector();
    void openPitchContextSettings();
    bool chordEditorActive() const;
    bool editedStepInRange(uint8_t& step) const;
    bool activateFocusedContextRow();
    void maybeCloseFromMacro(uint8_t indexInPage);
    bool focusedRowIsValueRow() const;
    bool focusedRowIsContextRow() const;
    bool focusedRowSupportsLocalVariation() const;
    bool canRetargetEditedDrumLane() const;
    bool focusedContextHasChild() const;
    bool canPasteFocusedStepContent() const;
    void resetFocusedValueRowToDefault();
    void clearFocusedContextChild();
    void copyFocusedStepContent();
    void pasteFocusedStepContent();
    bool beginPreparedPatternMutation(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
        core::state::sequencer::SequencerHistoryDescriptor descriptor,
        bool compactGraphOnSeal = false);
    bool sealPreparedPatternMutation(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key, bool changed,
        core::state::sequencer::SequencerHistoryDescriptor descriptor);
    bool commitPreparedPatternMutation(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner);
    void openStepPresetLibrary();
    void openChordPresetLibrary();
    void closePresetLibrary();
    void backFromPresetLibrary();
    void movePresetLibraryItem(float delta);
    void adjustPresetLibraryDetail(float delta);
    void enterPresetLibraryDetail();
    void togglePresetLibraryMode();
    void cyclePatternPresetLibrarySource();
    void beginPresetLibraryActionGuard();
    void releasePresetLibraryAction();
    void commitPresetLibraryActionGuard();
    void handlePresetLibraryResult(const SequencerPresetLibraryResult& result);

    ButtonReleaseLatch<2> context_release_latch_;
    ButtonReleaseLatch<1> preset_open_release_latch_;
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlay_state_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    core::state::StructureClipboardState& structure_clipboard_;
    core::state::TrackNavigationState& track_ui_;
    core::state::PatternPitchSettingsState& pattern_pitch_settings_;
    oc::state::Signal<core::state::StructureNavigationFocus,
                      core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    SequencerHistoryDomainServices history_;
    SequencerStepPresetDomainServices step_presets_;
    SequencerChordPresetDomainServices chord_presets_;
    SequencerPatternPresetDomainServices pattern_presets_;
    SequencerStepPresetLibraryAdapter step_preset_library_adapter_;
    SequencerChordPresetLibraryAdapter chord_preset_library_adapter_;
    SequencerPatternPresetLibraryAdapter pattern_preset_library_adapter_;
    SequencerPresetLibraryWorkflow preset_library_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
    oc::type::ScopeID preset_library_overlay_scope_ = 0;
    TimeProviderFn time_provider_ = core::time_compat::millis;
    bool preset_library_action_press_active_ = false;
    bool preset_library_auto_close_pending_ = false;
    bool step_retarget_active_ = false;
    bool lane_retarget_active_ = false;
    bool pitch_context_settings_open_ = false;
    uint32_t preset_library_auto_close_at_ms_ = 0;
};

}  // namespace core::handler
