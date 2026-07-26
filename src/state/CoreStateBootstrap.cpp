#include "state/CoreStateBootstrap.hpp"

#include <cstddef>
#include <memory>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/ChangeCoalescer.hpp>

#include "state/CoreState.hpp"
#include "state/CoreStateDiagnostics.hpp"
#include "state/CoreSettingsLayout.hpp"
#include "state/DataManagerWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::state {

namespace {
// Manual macro values update their page base immediately. Only the project
// mutation notification is coalesced so encoder-rate input does not
// continuously enqueue session saves.
constexpr uint32_t MACRO_VALUE_PROJECT_SAVE_DELAY_MS = 5000;
constexpr size_t SEQUENCER_COALESCER_SUBSCRIPTION_COUNT = 15;

[[noreturn]] FLASHMEM void failSequencerCoalescerSetup() {
    OC_LOG_ERROR("{}", "[CoreState] Sequencer mutation coalescer setup failed");
    while (true) {}
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
    // Project Track channel/mute mirrors are intentionally absent: their
    // canonical service already publishes dirty and runtime revisions once at
    // gesture commit. Watching the projected mirrors would duplicate it.

    if (!coalescer.valid() ||
        coalescer.subscriptionCount() != SEQUENCER_COALESCER_SUBSCRIPTION_COUNT) {
        failSequencerCoalescerSetup();
    }
}

FLASHMEM void CoreStateBootstrap::registerOverlaySignals_(CoreState& state) {
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT, state.macroEdit.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_SELECTOR, state.macroEdit.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_AUTOMATION, state.macroEdit.automationVisible);
    state.overlays.registerItem(core::ui::OverlayType::VIEW_SELECTOR, state.viewSelector.visible);

    state.overlays.registerItem(core::ui::OverlayType::SEQ_STEP_EDIT, state.sequencer.stepEdit.visible);
    state.overlays.registerItem(
        core::ui::OverlayType::SEQ_PATTERN_EDIT,
        state.sequencer.patternEditor.active
    );
    state.overlays.registerItem(
        core::ui::OverlayType::SEQ_TRACK_EDIT,
        state.projectTrackEditor
    );
    state.overlays.registerItem(
        core::ui::OverlayType::SEQ_STEP_PRESET,
        state.sequencer.stepPresetPicker.visible
    );
    state.overlays.registerItem(
        core::ui::OverlayType::SEQ_CC_LANE,
        state.sequencer.ccLaneUi.overlayVisible
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
    diagnostics::configureDebugLabels(state);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    registerOverlaySignals_(state);
    setupMutationCoalescing_(state);
    state.projectSessionTrackingEnabled_ = true;
}

}  // namespace core::state
