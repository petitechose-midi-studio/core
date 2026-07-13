#include "state/CoreStateBootstrap.hpp"

#include <cstddef>
#include <memory>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/ChangeCoalescer.hpp>

#include "state/CoreState.hpp"
#include "state/CoreSettingsLayout.hpp"
#include "state/DataManagerWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::state {

namespace {
// Manual macro values update their page base immediately. Only the project
// mutation notification is coalesced so encoder-rate input does not
// continuously enqueue session saves.
constexpr uint32_t MACRO_VALUE_PROJECT_SAVE_DELAY_MS = 5000;
constexpr size_t SEQUENCER_COALESCER_SUBSCRIPTION_COUNT = 17;

[[noreturn]] FLASHMEM void failSequencerCoalescerSetup() {
    OC_LOG_ERROR("{}", "[CoreState] Sequencer mutation coalescer setup failed");
    while (true) {}
}

void configureDebugLabels_(CoreState& state) {
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

    state.statusBar.pageName.setDebugLabel("core.statusBar.pageName");
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

    state.pages.selector.selectedIndex.setDebugLabel("core.macroPages.selector.selectedIndex");
    state.pages.selector.visible.setDebugLabel("core.macroPages.selector.visible");

    state.macroEdit.flowPhase.setDebugLabel("core.macroEdit.flowPhase");
    state.macroEdit.selector.visible.setDebugLabel("core.macroEdit.selector.visible");
    state.macroEdit.selector.editingRow.setDebugLabel("core.macroEdit.selector.editingRow");
    state.macroEdit.selector.selectedIndex.setDebugLabel("core.macroEdit.selector.selectedIndex");
    state.macroEdit.macroSelector.visible.setDebugLabel("core.macroEdit.macroSelector.visible");
    state.macroEdit.macroSelector.selectedIndex.setDebugLabel("core.macroEdit.macroSelector.selectedIndex");
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
    state.sequencerSettings.flowPhase.setDebugLabel("core.sequencerSettings.flowPhase");
    state.sequencerSettings.visible.setDebugLabel("core.sequencerSettings.visible");
    state.sequencerSettings.focusedRow.setDebugLabel("core.sequencerSettings.focusedRow");
    state.sequencerSettings.selector.visible.setDebugLabel("core.sequencerSettings.selector.visible");
    state.sequencerSettings.selector.selectedIndex.setDebugLabel("core.sequencerSettings.selector.selectedIndex");
    state.sequencerSettings.selector.editingRow.setDebugLabel("core.sequencerSettings.selector.editingRow");
    state.patternPitchSettings.flowPhase.setDebugLabel("core.patternPitchSettings.flowPhase");
    state.patternPitchSettings.visible.setDebugLabel("core.patternPitchSettings.visible");
    state.patternPitchSettings.focusedRow.setDebugLabel("core.patternPitchSettings.focusedRow");
    state.patternPitchSettings.selector.visible.setDebugLabel("core.patternPitchSettings.selector.visible");
    state.patternPitchSettings.selector.selectedIndex.setDebugLabel("core.patternPitchSettings.selector.selectedIndex");
    state.patternPitchSettings.selector.editingRow.setDebugLabel("core.patternPitchSettings.selector.editingRow");

    state.dataManager.context.setDebugLabel("core.dataManager.context");
    state.dataManager.flowPhase.setDebugLabel("core.dataManager.flowPhase");
    state.dataManager.dialog.visible.setDebugLabel("core.dataManager.dialog.visible");
    state.dataManager.dialog.mode.setDebugLabel("core.dataManager.dialog.mode");
    state.dataManager.dialog.selectedIndex.setDebugLabel("core.dataManager.dialog.selectedIndex");
    state.dataManager.dialog.editingShortcutRow.setDebugLabel("core.dataManager.dialog.editingShortcutRow");

    state.sequencer.stepPropertyInlineSelector.selecting.setDebugLabel("core.sequencer.stepPropertyInlineSelector.selecting");
    state.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive.setDebugLabel("core.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive");
    state.sequencer.stepPropertyInlineSelector.selectedIndex.setDebugLabel("core.sequencer.stepPropertyInlineSelector.selectedIndex");
    state.sequencer.stepEdit.contextHold.action.setDebugLabel("core.sequencer.stepEdit.contextHold.action");
    state.sequencer.stepEdit.contextHold.startedAtMs.setDebugLabel("core.sequencer.stepEdit.contextHold.startedAtMs");
    state.sequencer.stepPresetPicker.visible.setDebugLabel("core.sequencer.stepPresetPicker.visible");
    state.sequencer.stepPresetPicker.mode.setDebugLabel("core.sequencer.stepPresetPicker.mode");
    state.sequencer.stepPresetPicker.selectedIndex.setDebugLabel("core.sequencer.stepPresetPicker.selectedIndex");
    state.sequencer.stepPresetPicker.entryCount.setDebugLabel("core.sequencer.stepPresetPicker.entryCount");
    state.sequencer.stepPresetPicker.truncated.setDebugLabel("core.sequencer.stepPresetPicker.truncated");
    state.sequencer.patternQuickControls.selecting.setDebugLabel("core.sequencer.patternQuickControls.selecting");
    state.sequencer.patternQuickControls.physicalHoldActive.setDebugLabel("core.sequencer.patternQuickControls.physicalHoldActive");
    state.sequencer.patternQuickControls.focusedItem.setDebugLabel("core.sequencer.patternQuickControls.focusedItem");
    state.sequencer.patternQuickControls.offsetSteps.setDebugLabel("core.sequencer.patternQuickControls.offsetSteps");
    state.sequencer.pattern.patternVariationRevision.setDebugLabel("core.sequencer.pattern.patternVariationRevision");
    state.sequencer.pattern.patternScaleRevision.setDebugLabel("core.sequencer.pattern.patternScaleRevision");
    state.sequencer.pattern.patternTimingRevision.setDebugLabel("core.sequencer.pattern.patternTimingRevision");
    state.sequencer.pattern.swingOffsetPercent.setDebugLabel("core.sequencer.pattern.swingOffsetPercent");
    state.sequencer.pattern.patternNudgePercent.setDebugLabel("core.sequencer.pattern.patternNudgePercent");
    state.sequencer.contentView.revision.setDebugLabel("core.sequencer.contentView.revision");
    state.sequencer.variationTelemetryRevision.setDebugLabel("core.sequencer.variationTelemetryRevision");
    state.sequencerTracks.projectScaleRevisionSignal().setDebugLabel("core.sequencerTracks.projectScaleRevision");
    state.sequencerTracks.mutedMaskSignal().setDebugLabel("core.sequencerTracks.mutedMask");

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
}  // namespace

FLASHMEM void CoreStateBootstrap::initializeMacroPersistence_(CoreState& state) {
    state.macroDomain_.persistenceReady =
        state.macroPersistence.initStatus() == persistence::PersistenceWriteStatus::OK;
    if (!state.macroDomain_.persistenceReady) {
        OC_LOG_WARN("[CoreState] Macro persistence init failed");
    }
}

FLASHMEM void CoreStateBootstrap::initializeSequencerPersistence_(CoreState& state) {
    state.sequencerDomain_.persistenceReady =
        state.sequencerPersistence.initStatus() == persistence::PersistenceWriteStatus::OK;
    if (!state.sequencerDomain_.persistenceReady) {
        OC_LOG_WARN("[CoreState] Sequencer persistence init failed");
    }
}

FLASHMEM void CoreStateBootstrap::configureMacroMutationCoalescing_(CoreState& state) {
    state.macroDomain_.mutationCoalescer =
        std::make_unique<oc::state::ChangeCoalescer<>>(
            [&state]() { state.markProjectMutated(); },
            MACRO_VALUE_PROJECT_SAVE_DELAY_MS
        );
}

FLASHMEM void CoreStateBootstrap::configureSequencerMutationCoalescing_(CoreState& state) {
    state.sequencerDomain_.mutationCoalescer =
        std::make_unique<oc::state::ChangeCoalescer<SEQUENCER_COALESCER_SUBSCRIPTION_COUNT>>(
            [&state]() {
                state.markSequencerProjectMutated_();
            },
            CoreSettings::VALUE_SAVE_DELAY_MS
        );

    auto& coalescer = *state.sequencerDomain_.mutationCoalescer;
    coalescer.watch(state.sequencer.pattern.length);
    coalescer.watch(state.sequencer.pattern.stepsPerBeat);
    coalescer.watch(state.sequencer.pattern.midiChannel);
    coalescer.watch(state.sequencer.pattern.enabledMask);
    coalescer.watch(state.sequencer.pattern.stepDataRevision);
    coalescer.watch(state.sequencer.page);
    coalescer.watch(state.sequencer.focusedStep);
    coalescer.watch(state.sequencer.activeStepProperty);
    coalescer.watch(state.sequencerTracks.activeTrackSignal());
    coalescer.watch(state.sequencerTracks.enabledMaskSignal());
    coalescer.watch(state.sequencer.pattern.patternVariationRevision);
    coalescer.watch(state.sequencer.pattern.patternScaleRevision);
    coalescer.watch(state.sequencerTracks.projectScaleRevisionSignal());
    coalescer.watch(state.sequencer.pattern.patternTimingRevision);
    coalescer.watch(state.sequencer.pattern.swingOffsetPercent);
    coalescer.watch(state.sequencer.pattern.patternNudgePercent);
    coalescer.watch(state.sequencerTracks.mutedMaskSignal());

    if (!coalescer.valid() ||
        coalescer.subscriptionCount() != SEQUENCER_COALESCER_SUBSCRIPTION_COUNT) {
        failSequencerCoalescerSetup();
    }
}

FLASHMEM void CoreStateBootstrap::registerOverlaySignals_(CoreState& state) {
    state.overlays.registerItem(core::ui::OverlayType::PAGE_SELECTOR, state.pages.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT, state.macroEdit.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_SELECTOR, state.macroEdit.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR, state.macroEdit.macroSelector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_AUTOMATION, state.macroEdit.automationVisible);
    state.overlays.registerItem(core::ui::OverlayType::VIEW_SELECTOR, state.viewSelector.visible);

    state.overlays.registerItem(core::ui::OverlayType::SEQ_STEP_EDIT, state.sequencer.stepEdit.visible);
    state.overlays.registerItem(
        core::ui::OverlayType::SEQ_STEP_PRESET,
        state.sequencer.stepPresetPicker.visible
    );
    state.overlays.registerItem(
        core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR,
        state.deviceSettings.selector.visible
    );
    state.overlays.registerItem(core::ui::OverlayType::SEQUENCER_SETTINGS, state.sequencerSettings.visible);
    state.overlays.registerItem(core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR, state.sequencerSettings.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::PATTERN_PITCH_SETTINGS, state.patternPitchSettings.visible);
    state.overlays.registerItem(core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR, state.patternPitchSettings.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::DATA_MANAGER, state.dataManager.visible);
    state.overlays.registerItem(core::ui::OverlayType::DATA_MANAGER_DIALOG, state.dataManager.dialog.visible);
}

FLASHMEM void CoreStateBootstrap::initializePersistence_(CoreState& state) {
    state.sequencer.reset();
    state.sequencerTracks.reset();
    uint16_t persistedSharedTrackMask = core_settings::layout::DEFAULT_SHARED_TRACK_ENABLED_MASK;
    uint8_t persistedSharedTrackActive = core_settings::layout::DEFAULT_SHARED_TRACK_ACTIVE;
    state.settings.load(
        state.midiSync,
        persistedSharedTrackMask,
        persistedSharedTrackActive
    );
    DataManagerWorkflow::loadShortcutsFromSettings(DataManagerWorkflow::StateRefs{
        state.dataManager,
        state.settings,
    });
    state.setSharedTrackState_(
        persistedSharedTrackMask,
        persistedSharedTrackActive,
        false
    );
    initializeMacroPersistence_(state);
    initializeSequencerPersistence_(state);
}

FLASHMEM void CoreStateBootstrap::setupMutationCoalescing_(CoreState& state) {
    configureMacroMutationCoalescing_(state);
    configureSequencerMutationCoalescing_(state);
}

FLASHMEM void CoreStateBootstrap::initialize(CoreState& state) {
    initializePersistence_(state);
    configureDebugLabels_(state);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    registerOverlaySignals_(state);
    setupMutationCoalescing_(state);
    state.projectSessionTrackingEnabled_ = true;
}

}  // namespace core::state
