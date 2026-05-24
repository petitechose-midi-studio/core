#include "state/CoreStateLifecycle.hpp"

#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "state/CoreState.hpp"
#include "state/DataManagerWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

void CoreStateLifecycle::updateAutoPersist_(CoreState& state) {
    if (state.macroDomain_.autoPersist) {
        state.macroDomain_.autoPersist->update();
    }
    if (state.sequencerDomain_.autoPersist) {
        state.sequencerDomain_.autoPersist->update();
    }
}

void CoreStateLifecycle::updatePendingMacroWorkspacePersist_(CoreState& state) {
    if (!state.macroDomain_.workspacePersistPending ||
        state.macroDomain_.workspacePersistTimestampMs == 0) {
        return;
    }

    const uint32_t nowMs = oc::time::millis();
    if ((nowMs - state.macroDomain_.workspacePersistTimestampMs) <
        MacroDomainState::WORKSPACE_MUTATION_SAVE_DELAY_MS) {
        return;
    }

    if (state.macroDomain_.lastInteractionTimestampMs != 0 &&
        (nowMs - state.macroDomain_.lastInteractionTimestampMs) <
            MacroDomainState::WORKSPACE_MUTATION_SAVE_DELAY_MS) {
        return;
    }

    state.persistMacroWorkspaceNow_();
}

void CoreStateLifecycle::updatePendingSharedTrackPersist_(CoreState& state) {
    if (!state.sharedTrackPersistPending_ || state.sharedTrackPersistTimestampMs_ == 0) {
        return;
    }

    const uint32_t nowMs = oc::time::millis();
    if ((nowMs - state.sharedTrackPersistTimestampMs_) < CoreSettings::VALUE_SAVE_DELAY_MS) {
        return;
    }

    state.persistSharedTrackState_();
}

void CoreStateLifecycle::flushAutoPersist_(CoreState& state) {
    if (state.macroDomain_.autoPersist) {
        state.macroDomain_.autoPersist->flush();
    }
    if (state.sequencerDomain_.autoPersist) {
        state.sequencerDomain_.autoPersist->flush();
    }
}

void CoreStateLifecycle::flushPendingMacroWorkspacePersist_(CoreState& state) {
    if (!state.macroDomain_.workspacePersistPending) return;
    state.persistMacroWorkspaceNow_();
}

void CoreStateLifecycle::flushPendingSharedTrackPersist_(CoreState& state) {
    if (!state.sharedTrackPersistPending_) return;
    state.persistSharedTrackState_();
}

void CoreStateLifecycle::persistFactoryDefaults_(CoreState& state) {
    const auto saveStatus = state.settings.saveAllStatus(
        state.midiSync,
        state.sharedTrackEnabledMask.get(),
        state.sharedTrackActive.get()
    );
    if (saveStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Failed to persist default core settings during factory reset: {}",
                    persistence::persistenceWriteStatusLabel(saveStatus));
        return;
    }

    const auto shortcutStatus = state.settings.saveDefaultDataManagerShortcutsStatus();
    if (shortcutStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Failed to persist default Data Manager shortcuts during factory reset: {}",
                    persistence::persistenceWriteStatusLabel(shortcutStatus));
    }
}

void CoreStateLifecycle::resetMacroDomain_(CoreState& state) {
    state.pages.initDefaults();
    state.midiSync.reset();
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    DataManagerWorkflow::loadShortcutsFromSettings(DataManagerWorkflow::StateRefs{
        state.dataManager,
        state.settings,
    });
    state.persistMacroWorkspaceNow_();
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.macroEdit.reset();
    state.macroUi.reset();
    state.trackNavigation.reset();
}

void CoreStateLifecycle::resetSequencerDomain_(CoreState& state) {
    state.sequencer.reset();
    state.sequencerTracks.reset();
    sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer);
    if (state.sequencerDomain_.pendingApply) {
        state.sequencerDomain_.pendingApply->valid = false;
    }
    state.persistSequencerWorkspace_();
}

void CoreStateLifecycle::resetUiState_(CoreState& state) {
    state.viewSelector.reset();
    state.globalSettings.reset();
    state.sequencerSettings.reset();
    state.dataManager.resetSession(DataManagerContext::MACRO);
    state.dataManager.feedback.set("");
    state.macroUi.reset();
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    state.structureClipboard.clear();
    state.activeView.set(core::ui::ViewType::MACRO);
    state.overlays.hideAll();
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    state.refreshSharedTrackStateFromMacroPages_(false);
}

void CoreStateLifecycle::update(CoreState& state) {
    const uint32_t nowMs = oc::time::millis();
    state.statusBar.updateTransient(nowMs);
    applyPendingSequencerApplyIfReady(state);
    state.sequencer.updateUi(nowMs);
    updateAutoPersist_(state);
    updatePendingMacroWorkspacePersist_(state);
    updatePendingSharedTrackPersist_(state);
}

void CoreStateLifecycle::flush(CoreState& state) {
    flushAutoPersist_(state);
    flushPendingMacroWorkspacePersist_(state);
    flushPendingSharedTrackPersist_(state);
}

void CoreStateLifecycle::flushAutoPersist(CoreState& state) {
    flushAutoPersist_(state);
}

void CoreStateLifecycle::resetStandaloneTransientUi(CoreState& state) {
    state.macroEdit.reset();
    state.macroUi.reset();
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    state.structureClipboard.clear();
    state.sequencer.stepEdit.visible.set(false);
    state.sequencer.stepEdit.reset();
    state.sequencer.stepPropertyInlineSelector.reset();
    state.sequencer.patternQuickControls.reset();
    state.sequencer.structureUi.reset();
    state.globalSettings.reset();
    state.sequencerSettings.reset();
    state.dataManager.resetSession(DataManagerContext::MACRO);
}

void CoreStateLifecycle::factoryReset(CoreState& state) {
    const auto resetStatus = state.settings.factoryResetStatus();
    if (resetStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] CoreSettings factory reset failed: {}",
                    persistence::persistenceWriteStatusLabel(resetStatus));
    }
    resetMacroDomain_(state);
    resetSequencerDomain_(state);
    resetUiState_(state);
    state.sharedTrackPersistPending_ = false;
    state.sharedTrackPersistTimestampMs_ = 0;
    persistFactoryDefaults_(state);
}

void CoreStateLifecycle::queuePendingSequencerApply(CoreState& state,
                                                    const sequencer::SequencerState& staged,
                                                    bool merge) {
    if (!state.sequencerDomain_.pendingApply) return;
    sequencer::captureSnapshot(staged, state.sequencerDomain_.pendingApply->snapshot);
    state.sequencerDomain_.pendingApply->anchorPlayhead = state.sequencer.playheadStep.get();
    state.sequencerDomain_.pendingApply->merge = merge;
    state.sequencerDomain_.pendingApply->fullBank = false;
    state.sequencerDomain_.pendingApply->valid = true;
}

void CoreStateLifecycle::clearPendingSequencerApply(CoreState& state) {
    if (!state.sequencerDomain_.pendingApply) return;
    state.sequencerDomain_.pendingApply->valid = false;
    state.sequencerDomain_.pendingApply->fullBank = false;
}

void CoreStateLifecycle::applyPendingSequencerApplyIfReady(CoreState& state) {
    if (!state.sequencerDomain_.pendingApply || !state.sequencerDomain_.pendingApply->valid) return;

    if (state.statusBar.playing.get()) {
        const int16_t playhead = state.sequencer.playheadStep.get();
        if (playhead < 0) return;
        if (playhead == state.sequencerDomain_.pendingApply->anchorPlayhead) return;
    }

    if (state.sequencerDomain_.pendingApply->fullBank) {
        sequencer::applyTrackBankSnapshot(
            state.sequencerTracks,
            state.sequencer,
            state.sequencerDomain_.pendingApply->bankSnapshot
        );
    } else if (state.sequencerDomain_.pendingApply->merge) {
        sequencer::mergeSnapshotIntoCurrent(
            state.sequencer,
            state.sequencerDomain_.pendingApply->snapshot
        );
    } else {
        sequencer::applySnapshot(state.sequencer, state.sequencerDomain_.pendingApply->snapshot);
    }
    sequencer::storeActiveTrack(state.sequencerTracks, state.sequencer);
    state.refreshSharedTrackStateFromSequencer();
    state.sequencerDomain_.pendingApply->valid = false;
    state.persistSequencerWorkspace_();
}

}  // namespace core::state
