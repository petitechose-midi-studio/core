#include "state/CoreStateBootstrap.hpp"

#include <memory>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/AutoPersistIncremental.hpp>

#include "state/CoreState.hpp"
#include "state/CoreSettingsLayout.hpp"
#include "state/DataManagerWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::state {

namespace {
// Macro runtime values are projected back into the project snapshot after a
// short idle window so live movement does not continuously enqueue session
// saves.
constexpr uint32_t MACRO_VALUE_PROJECT_SAVE_DELAY_MS = 5000;

void configureDebugLabels_(CoreState& state) {
    state.activeView.setDebugLabel("core.activeView");

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
    state.macroUi.quickControlsSelecting.setDebugLabel("core.macroUi.quickControlsSelecting");
    state.macroUi.focusedQuickControl.setDebugLabel("core.macroUi.focusedQuickControl");
    state.macroUi.ccOffset.setDebugLabel("core.macroUi.ccOffset");
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

    state.projectNavigation.activeTab.setDebugLabel("core.projectNavigation.activeTab");
    state.projectNavigation.currentNode.setDebugLabel("core.projectNavigation.currentNode");
    state.projectNavigation.depth.setDebugLabel("core.projectNavigation.depth");
    state.projectNavigation.focusedRow.setDebugLabel("core.projectNavigation.focusedRow");
    state.projectNavigation.physicalHoldActive.setDebugLabel("core.projectNavigation.physicalHoldActive");
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

FLASHMEM void CoreStateBootstrap::configureMacroAutoPersist_(CoreState& state) {
    state.macroDomain_.autoPersist =
        std::make_unique<oc::state::AutoPersistIncremental<MACRO_COUNT>>(
            [&state](uint8_t i) {
                float value = state.macros.slots[i].value.get();
                auto& page = state.pages.activePageData();
                if (page.values[i] == value) return;
                page.values[i] = value;
            },
            [&state]() { state.markProjectMutated(); },
            MACRO_VALUE_PROJECT_SAVE_DELAY_MS
        );

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        state.macroDomain_.autoPersist->watchAt(i, state.macros.slots[i].value);
    }
}

FLASHMEM void CoreStateBootstrap::configureSequencerAutoPersist_(CoreState& state) {
    state.sequencerDomain_.autoPersist =
        std::make_unique<oc::state::AutoPersistIncremental<16>>(
            [](uint8_t) {},
            [&state]() {
                state.markSequencerProjectMutated_();
            },
            CoreSettings::VALUE_SAVE_DELAY_MS
        );

    state.sequencerDomain_.autoPersist->watchAt(0, state.sequencer.pattern.length);
    state.sequencerDomain_.autoPersist->watchAt(1, state.sequencer.pattern.stepsPerBeat);
    state.sequencerDomain_.autoPersist->watchAt(2, state.sequencer.pattern.midiChannel);
    state.sequencerDomain_.autoPersist->watchAt(3, state.sequencer.pattern.enabledMask);
    state.sequencerDomain_.autoPersist->watchAt(4, state.sequencer.pattern.stepDataRevision);
    state.sequencerDomain_.autoPersist->watchAt(5, state.sequencer.page);
    state.sequencerDomain_.autoPersist->watchAt(6, state.sequencer.focusedStep);
    state.sequencerDomain_.autoPersist->watchAt(7, state.sequencer.activeStepProperty);
    state.sequencerDomain_.autoPersist->watchAt(8, state.sequencerTracks.activeTrackSignal());
    state.sequencerDomain_.autoPersist->watchAt(9, state.sequencerTracks.enabledMaskSignal());
    state.sequencerDomain_.autoPersist->watchAt(10, state.sequencer.pattern.patternVariationRevision);
    state.sequencerDomain_.autoPersist->watchAt(11, state.sequencer.pattern.patternScaleRevision);
    state.sequencerDomain_.autoPersist->watchAt(12, state.sequencerTracks.projectScaleRevisionSignal());
    state.sequencerDomain_.autoPersist->watchAt(13, state.sequencer.pattern.patternTimingRevision);
    state.sequencerDomain_.autoPersist->watchAt(14, state.sequencer.pattern.swingOffsetPercent);
    state.sequencerDomain_.autoPersist->watchAt(15, state.sequencer.pattern.patternNudgePercent);
}

FLASHMEM void CoreStateBootstrap::registerOverlaySignals_(CoreState& state) {
    state.overlays.registerItem(core::ui::OverlayType::PAGE_SELECTOR, state.pages.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT, state.macroEdit.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_SELECTOR, state.macroEdit.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR, state.macroEdit.macroSelector.visible);
    state.overlays.registerItem(core::ui::OverlayType::VIEW_SELECTOR, state.viewSelector.visible);

    state.overlays.registerItem(core::ui::OverlayType::SEQ_STEP_EDIT, state.sequencer.stepEdit.visible);
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

FLASHMEM void CoreStateBootstrap::setupAutoPersist_(CoreState& state) {
    configureMacroAutoPersist_(state);
    configureSequencerAutoPersist_(state);
}

FLASHMEM void CoreStateBootstrap::initialize(CoreState& state) {
    initializePersistence_(state);
    configureDebugLabels_(state);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    registerOverlaySignals_(state);
    setupAutoPersist_(state);
    state.projectSessionTrackingEnabled_ = true;
}

}  // namespace core::state
