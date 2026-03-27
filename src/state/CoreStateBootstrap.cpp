#include "state/CoreStateBootstrap.hpp"

#include <memory>

#include <oc/log/Log.hpp>
#include <oc/state/AutoPersistIncremental.hpp>

#include "state/CoreState.hpp"

namespace core::state {

namespace {
// Macro workspace saves go through SD-backed persistence and can stall the UI
// during active performance. Keep the debounce high enough that live movement
// is not interrupted by periodic workspace flushes.
constexpr uint32_t MACRO_WORKSPACE_SAVE_DELAY_MS = 5000;
}

void CoreStateBootstrap::registerOverlaySignals_(CoreState& state) {
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

void CoreStateBootstrap::initializePersistence_(CoreState& state) {
    state.sequencer.reset();
    state.settings.load(state.pages, state.midiSync);
    DataManagerWorkflow::loadShortcutsFromSettings(state);

    state.macro_persistence_ready_ =
        state.macroPersistence.initStatus() == persistence::PersistenceWriteStatus::OK;
    if (state.macro_persistence_ready_) {
        if (!state.macroPersistence.loadWorkspace(state.pages)) {
            state.persistMacroWorkspace_();
        }
    } else {
        OC_LOG_WARN("[CoreState] Macro persistence init failed");
    }

    state.sequencer_persistence_ready_ =
        state.sequencerPersistence.initStatus() == persistence::PersistenceWriteStatus::OK;
    if (state.sequencer_persistence_ready_) {
        if (!state.sequencerPersistence.loadWorkspace(state.sequencer)) {
            state.persistSequencerWorkspace_();
        }
    } else {
        OC_LOG_WARN("[CoreState] Sequencer persistence init failed");
    }
}

void CoreStateBootstrap::setupAutoPersist_(CoreState& state) {
    state.macro_auto_persist_ = std::make_unique<oc::state::AutoPersistIncremental<MACRO_COUNT>>(
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
        state.macro_auto_persist_->watchAt(i, state.macros.slots[i].value);
    }

    state.sequencer_auto_persist_ = std::make_unique<oc::state::AutoPersistIncremental<8>>(
        [](uint8_t) {},
        [&state]() { state.persistSequencerWorkspace_(); },
        CoreSettings::VALUE_SAVE_DELAY_MS
    );

    state.sequencer_auto_persist_->watchAt(0, state.sequencer.length);
    state.sequencer_auto_persist_->watchAt(1, state.sequencer.stepsPerBeat);
    state.sequencer_auto_persist_->watchAt(2, state.sequencer.midiChannel);
    state.sequencer_auto_persist_->watchAt(3, state.sequencer.enabledMask);
    state.sequencer_auto_persist_->watchAt(4, state.sequencer.stepDataRevision);
    state.sequencer_auto_persist_->watchAt(5, state.sequencer.page);
    state.sequencer_auto_persist_->watchAt(6, state.sequencer.focusedStep);
    state.sequencer_auto_persist_->watchAt(7, state.sequencer.activeStepProperty);
}

void CoreStateBootstrap::initialize(CoreState& state) {
    initializePersistence_(state);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state);
    registerOverlaySignals_(state);
    setupAutoPersist_(state);
}

}  // namespace core::state
