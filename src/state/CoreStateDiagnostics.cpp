#include "state/CoreStateDiagnostics.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/Config.hpp>

#include "state/CoreState.hpp"

namespace core::state::diagnostics {

FLASHMEM void configureDebugLabels(CoreState& state) {
#if OC_ENABLE_STATS
    state.activeView.setDebugLabel("core.activeView");
    state.sharedTrackActive.setDebugLabel("core.sharedTrackActive");
    state.sharedTrackEnabledMask.setDebugLabel("core.sharedTrackEnabledMask");
    state.sequencerTracks.activeTrackSignal().setDebugLabel(
        "core.sequencerTracks.activeTrack"
    );
    state.sequencerTracks.enabledMaskSignal().setDebugLabel(
        "core.sequencerTracks.enabledMask"
    );

    static constexpr const char* MACRO_VALUE_LABELS[MACRO_COUNT] = {
        "core.macros.slot0.value", "core.macros.slot1.value",
        "core.macros.slot2.value", "core.macros.slot3.value",
        "core.macros.slot4.value", "core.macros.slot5.value",
        "core.macros.slot6.value", "core.macros.slot7.value",
    };
    static constexpr const char* MACRO_LABEL_LABELS[MACRO_COUNT] = {
        "core.macros.slot0.label", "core.macros.slot1.label",
        "core.macros.slot2.label", "core.macros.slot3.label",
        "core.macros.slot4.label", "core.macros.slot5.label",
        "core.macros.slot6.label", "core.macros.slot7.label",
    };
    static constexpr const char* MACRO_DISPLAY_LABELS[MACRO_COUNT] = {
        "core.macros.slot0.displayValue", "core.macros.slot1.displayValue",
        "core.macros.slot2.displayValue", "core.macros.slot3.displayValue",
        "core.macros.slot4.displayValue", "core.macros.slot5.displayValue",
        "core.macros.slot6.displayValue", "core.macros.slot7.displayValue",
    };
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        state.macros.slots[i].value.setDebugLabel(MACRO_VALUE_LABELS[i]);
        state.macros.slots[i].label.setDebugLabel(MACRO_LABEL_LABELS[i]);
        state.macros.slots[i].displayValue.setDebugLabel(MACRO_DISPLAY_LABELS[i]);
    }

    state.statusBar.noteInActive.setDebugLabel("core.statusBar.noteInActive");
    state.statusBar.noteOutActive.setDebugLabel("core.statusBar.noteOutActive");
    state.statusBar.ccInActive.setDebugLabel("core.statusBar.ccInActive");
    state.statusBar.ccOutActive.setDebugLabel("core.statusBar.ccOutActive");
    state.statusBar.playing.setDebugLabel("core.statusBar.playing");
    state.statusBar.tempo.setDebugLabel("core.statusBar.tempo");
    state.statusBar.tempoDisplay.setDebugLabel("core.statusBar.tempoDisplay");
    state.statusBar.syncExternalSource.setDebugLabel("core.statusBar.syncExternalSource");
    state.statusBar.syncInputPulse.setDebugLabel("core.statusBar.syncInputPulse");
    state.statusBar.tempoLocked.setDebugLabel("core.statusBar.tempoLocked");
    state.statusBar.transportLocked.setDebugLabel("core.statusBar.transportLocked");
    state.statusBar.beatPulse.setDebugLabel("core.statusBar.beatPulse");
    static constexpr const char* TRACK_ACTIVITY_LABELS[StatusBarState::TRACK_COUNT] = {
        "core.statusBar.trackNoteActivity0", "core.statusBar.trackNoteActivity1",
        "core.statusBar.trackNoteActivity2", "core.statusBar.trackNoteActivity3",
        "core.statusBar.trackNoteActivity4", "core.statusBar.trackNoteActivity5",
        "core.statusBar.trackNoteActivity6", "core.statusBar.trackNoteActivity7",
        "core.statusBar.trackNoteActivity8", "core.statusBar.trackNoteActivity9",
        "core.statusBar.trackNoteActivity10", "core.statusBar.trackNoteActivity11",
        "core.statusBar.trackNoteActivity12", "core.statusBar.trackNoteActivity13",
        "core.statusBar.trackNoteActivity14", "core.statusBar.trackNoteActivity15",
    };
    for (uint8_t i = 0; i < StatusBarState::TRACK_COUNT; ++i) {
        state.statusBar.trackNoteActivity[i].setDebugLabel(TRACK_ACTIVITY_LABELS[i]);
    }

    state.viewSelector.selectedIndex.setDebugLabel("core.viewSelector.selectedIndex");
    state.viewSelector.visible.setDebugLabel("core.viewSelector.visible");
    state.projectHistory.revision.setDebugLabel("core.projectHistory.revision");

    state.macroEdit.flowPhase.setDebugLabel("core.macroEdit.flowPhase");
    state.macroEdit.selector.visible.setDebugLabel("core.macroEdit.selector.visible");
    state.macroEdit.selector.editingRow.setDebugLabel("core.macroEdit.selector.editingRow");
    state.macroEdit.selector.selectedIndex.setDebugLabel("core.macroEdit.selector.selectedIndex");
    state.macroEdit.modulatorPickerIndex.setDebugLabel("core.macroEdit.modulatorPickerIndex");
    state.macroEdit.contextGuard.setDebugLabel("core.macroEdit.contextGuard");
    state.macroEdit.contextFeedback.setDebugLabel("core.macroEdit.contextFeedback");
    state.macroEdit.contextButton.setDebugLabel("core.macroEdit.contextButton");
    state.macroUi.activeProperty.setDebugLabel("core.macroUi.activeProperty");
    state.macroUi.clutchActive.setDebugLabel("core.macroUi.clutchActive");
    state.macroUi.focusedMacroSlot.setDebugLabel("core.macroUi.focusedMacroSlot");
    state.trackNavigation.previewAddSlot.setDebugLabel("core.trackNavigation.previewAddSlot");
    state.trackNavigation.previewTrackIndex.setDebugLabel("core.trackNavigation.previewTrackIndex");
    state.structureClipboard.revision.setDebugLabel("core.structureClipboard.revision");

    state.deviceSettings.flowPhase.setDebugLabel("core.deviceSettings.flowPhase");
    state.deviceSettings.selector.visible.setDebugLabel("core.deviceSettings.selector.visible");
    state.deviceSettings.selector.selectedIndex.setDebugLabel("core.deviceSettings.selector.selectedIndex");
    state.deviceSettings.selector.editingRow.setDebugLabel("core.deviceSettings.selector.editingRow");
    state.patternPitchSettings.flowPhase.setDebugLabel("core.patternPitchSettings.flowPhase");
    state.patternPitchSettings.visible.setDebugLabel("core.patternPitchSettings.visible");
    state.patternPitchSettings.focusedRow.setDebugLabel("core.patternPitchSettings.focusedRow");
    state.patternPitchSettings.selector.visible.setDebugLabel("core.patternPitchSettings.selector.visible");
    state.patternPitchSettings.selector.selectedIndex.setDebugLabel("core.patternPitchSettings.selector.selectedIndex");
    state.patternPitchSettings.selector.editingRow.setDebugLabel("core.patternPitchSettings.selector.editingRow");

    state.sequencer.stepPropertyInlineSelector.selecting.setDebugLabel("core.sequencer.stepPropertyInlineSelector.selecting");
    state.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive.setDebugLabel("core.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive");
    state.sequencer.stepPropertyInlineSelector.selectedIndex.setDebugLabel("core.sequencer.stepPropertyInlineSelector.selectedIndex");
    state.sequencer.stepEdit.contextHold.action.setDebugLabel("core.sequencer.stepEdit.contextHold.action");
    state.sequencer.stepEdit.contextHold.startedAtMs.setDebugLabel("core.sequencer.stepEdit.contextHold.startedAtMs");
    state.sequencer.presetLibrary.visible.setDebugLabel(
        "core.sequencer.presetLibrary.visible"
    );
    state.sequencer.presetLibrary.mode.setDebugLabel(
        "core.sequencer.presetLibrary.mode"
    );
    state.sequencer.presetLibrary.selectedIndex.setDebugLabel(
        "core.sequencer.presetLibrary.selectedIndex"
    );
    state.sequencer.presetLibrary.entryCount.setDebugLabel(
        "core.sequencer.presetLibrary.entryCount"
    );
    state.sequencer.presetLibrary.truncated.setDebugLabel(
        "core.sequencer.presetLibrary.truncated"
    );
    state.sequencer.patternQuickControls.selecting.setDebugLabel("core.sequencer.patternQuickControls.selecting");
    state.sequencer.patternQuickControls.focusedItem.setDebugLabel("core.sequencer.patternQuickControls.focusedItem");
    state.sequencer.patternQuickControls.offsetSteps.setDebugLabel("core.sequencer.patternQuickControls.offsetSteps");
    state.sequencer.patternEditor.active.setDebugLabel(
        "core.sequencer.patternEditor.active"
    );
    state.sequencer.pattern.patternVariationRevision.setDebugLabel("core.sequencer.pattern.patternVariationRevision");
    state.sequencer.pattern.patternScaleRevision.setDebugLabel("core.sequencer.pattern.patternScaleRevision");
    state.sequencer.pattern.patternTimingRevision.setDebugLabel("core.sequencer.pattern.patternTimingRevision");
    state.sequencer.pattern.swingOffsetPercent.setDebugLabel("core.sequencer.pattern.swingOffsetPercent");
    state.sequencer.pattern.patternNudgePercent.setDebugLabel("core.sequencer.pattern.patternNudgePercent");
    state.sequencer.contentView.revision.setDebugLabel("core.sequencer.contentView.revision");
    state.sequencer.variationTelemetryRevision.setDebugLabel("core.sequencer.variationTelemetryRevision");
    state.sequencerTracks.projectScaleRevisionSignal().setDebugLabel("core.sequencerTracks.projectScaleRevision");

    state.projectNavigation.activeTab.setDebugLabel("core.projectNavigation.activeTab");
    state.projectNavigation.currentNode.setDebugLabel("core.projectNavigation.currentNode");
    state.projectNavigation.depth.setDebugLabel("core.projectNavigation.depth");
    state.projectNavigation.focusedRow.setDebugLabel("core.projectNavigation.focusedRow");
    state.projectNavigation.physicalHoldActive.setDebugLabel("core.projectNavigation.physicalHoldActive");
    state.projectNavigation.contentRevision.setDebugLabel("core.projectNavigation.contentRevision");
    state.projectNavigation.lifecycleFeedback.setDebugLabel("core.projectNavigation.lifecycleFeedback");
#else
    (void)state;
#endif
}

}  // namespace core::state::diagnostics
