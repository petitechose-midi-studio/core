#include "state/CoreStateLifecycle.hpp"

#include <utility>

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

FLASHMEM void installCapturedGraph(sequencer::SequencerPatternState& target,
                                   GraphPtr& graph,
                                   uint32_t revision) {
    target.graph = std::move(graph);
    target.graphRevision.set(revision);
}

FLASHMEM void takeTrackBankGraphs(SequencerDomainState::PendingApply& pending,
                                  sequencer::SequencerTrackBankState& stagedBank) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        pending.bankGraphs[i] = std::move(stagedBank.track(i).graph);
    }
}

FLASHMEM void applyTrackBankGraphs(sequencer::SequencerTrackBankState& bank,
                                   SequencerDomainState::PendingApply& pending) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        installCapturedGraph(
            bank.track(i),
            pending.bankGraphs[i],
            pending.bankSnapshot.tracks[i].graphRevision
        );
    }
}

FLASHMEM void clearCapturedGraphs(SequencerDomainState::PendingApply& pending) {
    pending.patternGraph.reset();
    pending.activeTrackGraph.reset();
    for (auto& graph : pending.bankGraphs) {
        graph.reset();
    }
}

FLASHMEM bool prepareActiveTrackGraph(
    const sequencer::SequencerPatternState& source,
    GraphPtr& prepared
) {
    prepared.reset();
    if (!source.graph) return true;

    prepared = core::app::makeExtmemUnique<StepSequencerGraph>(*source.graph);
    return static_cast<bool>(prepared);
}

FLASHMEM void installPreparedActiveTrack(
    sequencer::SequencerTrackBankState& bank,
    const sequencer::SequencerState& editor,
    GraphPtr& graph
) {
    auto& activeTrack = bank.track(bank.activeTrackIndex());
    sequencer::copyPatternStatePreservingGraph(activeTrack, editor.pattern);
    installCapturedGraph(activeTrack, graph, editor.pattern.graphRevision.get());
}

}  // namespace

void CoreStateLifecycle::updateMutationCoalescers_(CoreState& state) {
    if (state.macroDomain_.mutationCoalescer) {
        state.macroDomain_.mutationCoalescer->update();
    }
    if (state.sequencerDomain_.mutationCoalescer) {
        state.sequencerDomain_.mutationCoalescer->update();
    }
}

void CoreStateLifecycle::updatePendingSharedTrackPersist_(CoreState& state) {
    if (!state.sharedTrackPersistPending_) return;

    const uint32_t nowMs = oc::time::millis();
    if ((nowMs - state.sharedTrackPersistTimestampMs_) < CoreSettings::VALUE_SAVE_DELAY_MS) {
        return;
    }

    state.persistSharedTrackState_();
}

FLASHMEM void CoreStateLifecycle::flushMutationCoalescers_(CoreState& state) {
    if (state.macroDomain_.mutationCoalescer) {
        state.macroDomain_.mutationCoalescer->flush();
    }
    if (state.sequencerDomain_.mutationCoalescer) {
        state.sequencerDomain_.mutationCoalescer->flush();
    }
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
    if (!sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to initialize sequencer track bank");
    }
    if (state.sequencerDomain_.pendingApply) {
        state.sequencerDomain_.pendingApply->valid = false;
        clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
    }
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
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::TRACK);
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
    updateMutationCoalescers_(state);
    updatePendingSharedTrackPersist_(state);
}

FLASHMEM void CoreStateLifecycle::flush(CoreState& state) {
    flushMutationCoalescers_(state);
    flushPendingSharedTrackPersist_(state);
}

FLASHMEM void CoreStateLifecycle::flushProjectMutationCoalescing(CoreState& state) {
    flushMutationCoalescers_(state);
}

FLASHMEM void CoreStateLifecycle::resetStandaloneTransientUi(CoreState& state) {
    state.macroEdit.reset();
    state.macroUi.reset();
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    state.structureClipboard.clear();
    state.sequencer.stepEdit.visible.set(false);
    state.sequencer.stepEdit.reset();
    state.sequencer.stepPresetPicker.reset();
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
    if (!sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to initialize sequencer track bank");
    }
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
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    state.structureClipboard.clear();
    state.sequencer.stepEdit.visible.set(false);
    state.sequencer.stepEdit.reset();
    state.sequencer.stepPresetPicker.reset();
    state.sequencer.stepPropertyInlineSelector.reset();
    state.sequencer.patternQuickControls.reset();
    state.sequencer.structureUi.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.patternPitchSettings.reset();
    state.projectNavigation.reset();

    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));

    flushMutationCoalescers_(state);
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

FLASHMEM bool CoreStateLifecycle::queuePendingSequencerApply(
    CoreState& state,
    sequencer::SequencerState& staged,
    bool merge
) {
    if (!state.sequencerDomain_.pendingApply) return false;

    GraphPtr activeTrackGraph;
    if (!prepareActiveTrackGraph(staged.pattern, activeTrackGraph)) return false;

    clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
    sequencer::captureSnapshot(staged.pattern, state.sequencerDomain_.pendingApply->snapshot);
    state.sequencerDomain_.pendingApply->patternGraph = std::move(staged.pattern.graph);
    state.sequencerDomain_.pendingApply->activeTrackGraph = std::move(activeTrackGraph);
    state.sequencerDomain_.pendingApply->anchorPlayhead = state.sequencer.playheadStep.get();
    state.sequencerDomain_.pendingApply->merge = merge;
    state.sequencerDomain_.pendingApply->fullBank = false;
    state.sequencerDomain_.pendingApply->valid = true;
    return true;
}

FLASHMEM bool CoreStateLifecycle::queuePendingSequencerBankApply(
    CoreState& state,
    sequencer::SequencerTrackBankState& stagedBank,
    sequencer::SequencerState& staged
) {
    if (!state.sequencerDomain_.pendingApply) return false;
    clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
    sequencer::captureTrackBankSnapshot(
        stagedBank,
        staged,
        state.sequencerDomain_.pendingApply->bankSnapshot
    );
    takeTrackBankGraphs(*state.sequencerDomain_.pendingApply, stagedBank);
    state.sequencerDomain_.pendingApply->patternGraph = std::move(staged.pattern.graph);
    state.sequencerDomain_.pendingApply->anchorPlayhead = state.sequencer.playheadStep.get();
    state.sequencerDomain_.pendingApply->merge = false;
    state.sequencerDomain_.pendingApply->fullBank = true;
    state.sequencerDomain_.pendingApply->valid = true;
    return true;
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
        installCapturedGraph(
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
        installCapturedGraph(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternGraph,
            state.sequencerDomain_.pendingApply->snapshot.graphRevision
        );
        installPreparedActiveTrack(
            state.sequencerTracks,
            state.sequencer,
            state.sequencerDomain_.pendingApply->activeTrackGraph
        );
    } else {
        sequencer::applySnapshotToEditor(state.sequencer, state.sequencerDomain_.pendingApply->snapshot);
        installCapturedGraph(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternGraph,
            state.sequencerDomain_.pendingApply->snapshot.graphRevision
        );
        installPreparedActiveTrack(
            state.sequencerTracks,
            state.sequencer,
            state.sequencerDomain_.pendingApply->activeTrackGraph
        );
    }
    state.markProjectMutated();
    state.refreshSharedTrackStateFromSequencer();
    state.clearSequencerHistory();
    state.sequencerDomain_.pendingApply->valid = false;
    clearCapturedGraphs(*state.sequencerDomain_.pendingApply);
}

}  // namespace core::state
