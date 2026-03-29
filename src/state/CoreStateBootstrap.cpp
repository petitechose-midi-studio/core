#include "state/CoreStateBootstrap.hpp"

#include <memory>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/AutoPersistIncremental.hpp>

#include "state/CoreState.hpp"
#include "state/DataManagerWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

namespace {
// Macro workspace saves go through SD-backed persistence and can stall the UI
// during active performance. Keep the debounce high enough that live movement
// is not interrupted by periodic workspace flushes.
constexpr uint32_t MACRO_WORKSPACE_SAVE_DELAY_MS = 5000;
}  // namespace

FLASHMEM void CoreStateBootstrap::initializeMacroPersistence_(CoreState& state) {
    state.macroDomain_.persistenceReady =
        state.macroPersistence.initStatus() == persistence::PersistenceWriteStatus::OK;
    if (!state.macroDomain_.persistenceReady) {
        OC_LOG_WARN("[CoreState] Macro persistence init failed");
        return;
    }

    if (!state.macroPersistence.loadWorkspace(state.pages)) {
        state.persistMacroWorkspace_();
    }
}

FLASHMEM void CoreStateBootstrap::initializeSequencerPersistence_(CoreState& state) {
    state.sequencerDomain_.persistenceReady =
        state.sequencerPersistence.initStatus() == persistence::PersistenceWriteStatus::OK;
    if (!state.sequencerDomain_.persistenceReady) {
        OC_LOG_WARN("[CoreState] Sequencer persistence init failed");
        return;
    }

    if (!state.sequencerPersistence.loadWorkspace(state.sequencerTracks, state.sequencer)) {
        state.persistSequencerWorkspace_();
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
            [&state]() { state.persistMacroWorkspace_(); },
            MACRO_WORKSPACE_SAVE_DELAY_MS
        );

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        state.macroDomain_.autoPersist->watchAt(i, state.macros.slots[i].value);
    }
}

FLASHMEM void CoreStateBootstrap::configureSequencerAutoPersist_(CoreState& state) {
    state.sequencerDomain_.autoPersist =
        std::make_unique<oc::state::AutoPersistIncremental<8>>(
            [](uint8_t) {},
            [&state]() { state.persistSequencerWorkspace_(); },
            CoreSettings::VALUE_SAVE_DELAY_MS
        );

    state.sequencerDomain_.autoPersist->watchAt(0, state.sequencer.length);
    state.sequencerDomain_.autoPersist->watchAt(1, state.sequencer.stepsPerBeat);
    state.sequencerDomain_.autoPersist->watchAt(2, state.sequencer.midiChannel);
    state.sequencerDomain_.autoPersist->watchAt(3, state.sequencer.enabledMask);
    state.sequencerDomain_.autoPersist->watchAt(4, state.sequencer.stepDataRevision);
    state.sequencerDomain_.autoPersist->watchAt(5, state.sequencer.page);
    state.sequencerDomain_.autoPersist->watchAt(6, state.sequencer.focusedStep);
    state.sequencerDomain_.autoPersist->watchAt(7, state.sequencer.activeStepProperty);
}

FLASHMEM void CoreStateBootstrap::registerOverlaySignals_(CoreState& state) {
    state.overlays.registerItem(core::ui::OverlayType::PAGE_SELECTOR, state.pages.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT, state.macroEdit.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_SELECTOR, state.macroEdit.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR, state.macroEdit.macroSelector.visible);
    state.overlays.registerItem(core::ui::OverlayType::VIEW_SELECTOR, state.viewSelector.visible);

    state.overlays.registerItem(core::ui::OverlayType::SEQ_STEP_EDIT, state.sequencer.stepEdit.visible);
    state.overlays.registerItem(core::ui::OverlayType::GLOBAL_SETTINGS, state.globalSettings.visible);
    state.overlays.registerItem(core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR, state.globalSettings.selector.visible);
    state.overlays.registerItem(core::ui::OverlayType::DATA_MANAGER, state.dataManager.visible);
    state.overlays.registerItem(core::ui::OverlayType::DATA_MANAGER_DIALOG, state.dataManager.dialog.visible);
}

FLASHMEM void CoreStateBootstrap::initializePersistence_(CoreState& state) {
    state.sequencer.reset();
    state.sequencerTracks.reset();
    state.settings.load(state.pages, state.midiSync);
    DataManagerWorkflow::loadShortcutsFromSettings(state);
    initializeMacroPersistence_(state);
    initializeSequencerPersistence_(state);
}

FLASHMEM void CoreStateBootstrap::setupAutoPersist_(CoreState& state) {
    configureMacroAutoPersist_(state);
    configureSequencerAutoPersist_(state);
}

FLASHMEM void CoreStateBootstrap::initialize(CoreState& state) {
    initializePersistence_(state);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state);
    registerOverlaySignals_(state);
    setupAutoPersist_(state);
}

}  // namespace core::state
