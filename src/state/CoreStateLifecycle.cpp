#include "state/CoreStateLifecycle.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "state/CoreState.hpp"
#include "state/DataManagerWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

namespace {

using StepSequencerGraph = oc::note::sequencer::StepSequencerGraph;
using GraphPtr = core::app::ExtmemUniquePtr<StepSequencerGraph>;

FLASHMEM void captureGraph(GraphPtr& target, const sequencer::SequencerPatternState& source) {
    if (!source.graph || !source.graph->enabled) {
        target.reset();
        return;
    }

    target = core::app::makeExtmemUnique<StepSequencerGraph>();
    if (target) {
        *target = *source.graph;
    }
}

FLASHMEM void applyCapturedGraph(sequencer::SequencerPatternState& target,
                                 const GraphPtr& graph,
                                 uint32_t revision) {
    if (!graph || !graph->enabled) {
        target.graph.reset();
        target.graphRevision.set(revision);
        return;
    }

    if (!target.graph) {
        target.graph = core::app::makeExtmemUnique<StepSequencerGraph>();
    }
    if (!target.graph) return;
    *target.graph = *graph;
    target.graphRevision.set(revision);
}

FLASHMEM void captureTrackBankGraphs(SequencerDomainState::PendingApply& pending,
                                     const sequencer::SequencerTrackBankState& stagedBank) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        captureGraph(pending.bankGraphs[i], stagedBank.track(i));
    }
}

FLASHMEM void applyTrackBankGraphs(sequencer::SequencerTrackBankState& bank,
                                   SequencerDomainState::PendingApply& pending) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        applyCapturedGraph(
            bank.track(i),
            pending.bankGraphs[i],
            pending.bankSnapshot.tracks[i].graphRevision
        );
    }
}

FLASHMEM void clearCapturedGraphs(SequencerDomainState::PendingApply& pending) {
    pending.patternGraph.reset();
    for (auto& graph : pending.bankGraphs) {
        graph.reset();
    }
}

}  // namespace

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

FLASHMEM void CoreStateLifecycle::flushAutoPersist_(CoreState& state) {
    if (state.macroDomain_.autoPersist) {
        state.macroDomain_.autoPersist->flush();
    }
    if (state.sequencerDomain_.autoPersist) {
        state.sequencerDomain_.autoPersist->flush();
    }
}

FLASHMEM void CoreStateLifecycle::flushPendingMacroWorkspacePersist_(CoreState& state) {
    if (!state.macroDomain_.workspacePersistPending) return;
    state.persistMacroWorkspaceNow_();
}

FLASHMEM void CoreStateLifecycle::flushPendingSharedTrackPersist_(CoreState& state) {
    if (!state.sharedTrackPersistPending_) return;
    state.persistSharedTrackState_();
}

FLASHMEM void CoreStateLifecycle::persistFactoryDefaults_(CoreState& state) {
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

FLASHMEM void CoreStateLifecycle::resetMacroDomain_(CoreState& state) {
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

FLASHMEM void CoreStateLifecycle::resetSequencerDomain_(CoreState& state) {
    state.sequencerDomain_.coalescedPatternHistory.clear();
    state.sequencer.reset();
    state.sequencerTracks.reset();
    state.sequencerHistory.clear();
    sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer);
    if (state.sequencerDomain_.pendingApply) {
        state.sequencerDomain_.pendingApply->valid = false;
        clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
    }
    state.persistSequencerWorkspace_();
}

FLASHMEM void CoreStateLifecycle::resetUiState_(CoreState& state) {
    state.viewSelector.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.patternPitchSettings.reset();
    state.dataManager.resetSession(DataManagerContext::MACRO);
    state.dataManager.feedback.set("");
    state.macroUi.reset();
    state.projectNavigation.reset();
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
    state.updateSequencerPatternHistoryCoalescing(nowMs);
    updateAutoPersist_(state);
    updatePendingMacroWorkspacePersist_(state);
    updatePendingSharedTrackPersist_(state);
}

FLASHMEM void CoreStateLifecycle::flush(CoreState& state) {
    flushAutoPersist_(state);
    flushPendingMacroWorkspacePersist_(state);
    flushPendingSharedTrackPersist_(state);
}

FLASHMEM void CoreStateLifecycle::flushAutoPersist(CoreState& state) {
    flushAutoPersist_(state);
}

FLASHMEM void CoreStateLifecycle::resetStandaloneTransientUi(CoreState& state) {
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
    state.projectNavigation.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.dataManager.resetSession(DataManagerContext::MACRO);
}

FLASHMEM void CoreStateLifecycle::resetMusicalProject(CoreState& state) {
    state.project.reset();
    state.pages.initDefaults();

    state.sequencerDomain_.coalescedPatternHistory.clear();
    state.sequencer.reset();
    state.sequencerTracks.reset();
    state.sequencerHistory.clear();
    sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer);
    clearPendingSequencerApply(state);

    state.setSharedTrackState_(macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK, 0, false);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.statusBar.tempo.set(120.0f);
    if (!state.statusBar.tempoLocked.get()) {
        state.statusBar.tempoDisplay.set(120.0f);
    }
    state.statusBar.pageName.set(state.pages.activePageData().name);

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
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.patternPitchSettings.reset();
    state.projectNavigation.reset();

    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));

    flushAutoPersist_(state);
    state.persistMacroWorkspaceNow_();
    state.persistSequencerWorkspace_();
}

FLASHMEM void CoreStateLifecycle::factoryReset(CoreState& state) {
    const auto resetStatus = state.settings.factoryResetStatus();
    if (resetStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] CoreSettings factory reset failed: {}",
                    persistence::persistenceWriteStatusLabel(resetStatus));
    }
    resetMacroDomain_(state);
    resetSequencerDomain_(state);
    state.project.reset();
    resetUiState_(state);
    state.sharedTrackPersistPending_ = false;
    state.sharedTrackPersistTimestampMs_ = 0;
    persistFactoryDefaults_(state);
}

FLASHMEM void CoreStateLifecycle::queuePendingSequencerApply(
    CoreState& state,
    const sequencer::SequencerState& staged,
    bool merge
) {
    if (!state.sequencerDomain_.pendingApply) return;
    clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
    sequencer::captureSnapshot(staged.pattern, state.sequencerDomain_.pendingApply->snapshot);
    captureGraph(state.sequencerDomain_.pendingApply->patternGraph, staged.pattern);
    state.sequencerDomain_.pendingApply->anchorPlayhead = state.sequencer.playheadStep.get();
    state.sequencerDomain_.pendingApply->merge = merge;
    state.sequencerDomain_.pendingApply->fullBank = false;
    state.sequencerDomain_.pendingApply->valid = true;
}

FLASHMEM void CoreStateLifecycle::queuePendingSequencerBankApply(
    CoreState& state,
    const sequencer::SequencerTrackBankState& stagedBank,
    const sequencer::SequencerState& staged
) {
    if (!state.sequencerDomain_.pendingApply) return;
    clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
    sequencer::captureTrackBankSnapshot(
        stagedBank,
        staged,
        state.sequencerDomain_.pendingApply->bankSnapshot
    );
    captureTrackBankGraphs(*state.sequencerDomain_.pendingApply, stagedBank);
    captureGraph(state.sequencerDomain_.pendingApply->patternGraph, staged.pattern);
    state.sequencerDomain_.pendingApply->anchorPlayhead = state.sequencer.playheadStep.get();
    state.sequencerDomain_.pendingApply->merge = false;
    state.sequencerDomain_.pendingApply->fullBank = true;
    state.sequencerDomain_.pendingApply->valid = true;
}

FLASHMEM void CoreStateLifecycle::clearPendingSequencerApply(CoreState& state) {
    if (!state.sequencerDomain_.pendingApply) return;
    state.sequencerDomain_.pendingApply->valid = false;
    state.sequencerDomain_.pendingApply->fullBank = false;
    clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
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
        applyTrackBankGraphs(state.sequencerTracks, *state.sequencerDomain_.pendingApply);
        applyCapturedGraph(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternGraph,
            state.sequencerDomain_.pendingApply
                ->bankSnapshot
                .tracks[state.sequencerDomain_.pendingApply->bankSnapshot.activeTrack]
                .graphRevision
        );
    } else if (state.sequencerDomain_.pendingApply->merge) {
        sequencer::mergeSnapshotIntoCurrent(
            state.sequencer,
            state.sequencerDomain_.pendingApply->snapshot
        );
        applyCapturedGraph(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternGraph,
            state.sequencerDomain_.pendingApply->snapshot.graphRevision
        );
    } else {
        sequencer::applySnapshotToEditor(state.sequencer, state.sequencerDomain_.pendingApply->snapshot);
        applyCapturedGraph(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternGraph,
            state.sequencerDomain_.pendingApply->snapshot.graphRevision
        );
    }
    sequencer::storeActiveTrack(state.sequencerTracks, state.sequencer);
    state.refreshSharedTrackStateFromSequencer();
    state.clearSequencerHistory();
    state.sequencerDomain_.pendingApply->valid = false;
    clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
    state.persistSequencerWorkspace_();
}

}  // namespace core::state
