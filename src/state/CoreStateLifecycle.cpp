#include "state/CoreStateLifecycle.hpp"

#include <oc/time/Time.hpp>

#include "state/CoreState.hpp"

namespace core::state {

void CoreStateLifecycle::update(CoreState& state) {
    const uint32_t nowMs = oc::time::millis();
    state.statusBar.updateTransient(nowMs);
    applyPendingSequencerApplyIfReady(state);
    state.sequencer.updateUi(nowMs);

    if (state.macro_auto_persist_) {
        state.macro_auto_persist_->update();
    }
    if (state.sequencer_auto_persist_) {
        state.sequencer_auto_persist_->update();
    }
}

void CoreStateLifecycle::flush(CoreState& state) {
    if (state.macro_auto_persist_) {
        state.macro_auto_persist_->flush();
    }
    if (state.sequencer_auto_persist_) {
        state.sequencer_auto_persist_->flush();
    }
}

void CoreStateLifecycle::factoryReset(CoreState& state) {
    const auto resetStatus = state.settings.factoryResetStatus();
    if (resetStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] CoreSettings factory reset failed: {}",
                    persistence::persistenceWriteStatusLabel(resetStatus));
    }
    state.pages.initDefaults();
    state.midiSync.reset();
    macro::MacroWorkflow::syncRuntimeFromActivePage(state);
    const auto saveStatus = state.settings.saveAllStatus(state.pages, state.midiSync);
    if (saveStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Failed to persist default core settings during factory reset: {}",
                    persistence::persistenceWriteStatusLabel(saveStatus));
    }
    DataManagerWorkflow::loadShortcutsFromSettings(state);
    state.persistMacroWorkspace_();
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.macroEdit.reset();
    state.viewSelector.reset();
    state.sequencer.reset();
    state.pending_sequencer_apply_.valid = false;
    state.persistSequencerWorkspace_();
    state.globalSettings.reset();
    state.dataManager.resetSession(DataManagerContext::MACRO);
    state.dataManager.feedback.set("");
    state.activeView.set(core::ui::ViewType::MACRO);
    state.overlays.hideAll();
    state.configRevision.set(state.configRevision.get() + 1);
}

void CoreStateLifecycle::queuePendingSequencerApply(CoreState& state,
                                                    const sequencer::SequencerState& staged,
                                                    bool merge) {
    sequencer::captureSnapshot(staged, state.pending_sequencer_apply_.snapshot);
    state.pending_sequencer_apply_.anchorPlayhead = state.sequencer.playheadStep.get();
    state.pending_sequencer_apply_.merge = merge;
    state.pending_sequencer_apply_.valid = true;
}

void CoreStateLifecycle::clearPendingSequencerApply(CoreState& state) {
    state.pending_sequencer_apply_.valid = false;
}

void CoreStateLifecycle::applyPendingSequencerApplyIfReady(CoreState& state) {
    if (!state.pending_sequencer_apply_.valid) return;

    if (state.statusBar.playing.get()) {
        const int16_t playhead = state.sequencer.playheadStep.get();
        if (playhead < 0) return;
        if (playhead == state.pending_sequencer_apply_.anchorPlayhead) return;
    }

    if (state.pending_sequencer_apply_.merge) {
        sequencer::mergeSnapshotIntoCurrent(state.sequencer, state.pending_sequencer_apply_.snapshot);
    } else {
        sequencer::applySnapshot(state.sequencer, state.pending_sequencer_apply_.snapshot);
    }
    state.pending_sequencer_apply_.valid = false;
    state.persistSequencerWorkspace_();
}

}  // namespace core::state
