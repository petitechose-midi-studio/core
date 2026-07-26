#include "state/CoreStateLifecycle.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "state/CoreState.hpp"
#include "state/DataManagerWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

namespace {

using StepSequencerGraph = oc::note::sequencer::StepSequencerGraph;
using GraphPtr = core::app::ExtmemUniquePtr<StepSequencerGraph>;
using CcLanePtr = sequencer::SequencerCcLaneBankPtr;

FLASHMEM void reprojectActiveMacroManualOverrides(CoreState& state) {
    const uint8_t track = state.pages.currentActiveTrack();
    const uint8_t page = state.pages.currentActivePage();
    state.macroUi.refreshManualOverrideMask(track, page);

    for (uint8_t macro = 0; macro < core::state::macro::MACRO_COUNT; ++macro) {
        float manualValue = 0.0f;
        if (!state.macroUi.manualOverrides.valueFor(
                core::state::macro::MacroAutomationSlotAddress{
                    .track = track,
                    .page = page,
                    .macro = macro,
                },
                manualValue
            )) {
            continue;
        }
        core::state::macro::MacroWorkflow::setRuntimeValue(state.macros, macro, manualValue);
    }
}

FLASHMEM void installCapturedGraph(sequencer::SequencerPatternState& target,
                                   GraphPtr& graph,
                                   uint32_t revision) {
    target.graph = std::move(graph);
    target.graphRevision.set(revision);
}

FLASHMEM void installCapturedCcLanes(
    sequencer::SequencerPatternState& target,
    CcLanePtr& lanes,
    uint32_t revision
) {
    sequencer::installSequencerCcLaneBank(target, std::move(lanes));
    target.ccLaneRevision.set(revision);
}

FLASHMEM void takeTrackBankPayloads(
    SequencerDomainState::PendingApply& pending,
    sequencer::SequencerTrackBankState& stagedBank
) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        pending.bankGraphs[i] = std::move(stagedBank.track(i).graph);
        pending.bankCcLanes[i] = std::move(stagedBank.track(i).ccLanes);
        pending.bankCcLaneRevisions[i] = stagedBank.track(i).ccLaneRevision.get();
    }
}

FLASHMEM void applyTrackBankPayloads(
    sequencer::SequencerTrackBankState& bank,
    SequencerDomainState::PendingApply& pending
) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        installCapturedGraph(
            bank.track(i),
            pending.bankGraphs[i],
            pending.bankSnapshot.tracks[i].graphRevision
        );
        installCapturedCcLanes(
            bank.track(i),
            pending.bankCcLanes[i],
            pending.bankCcLaneRevisions[i]
        );
    }
}

FLASHMEM void clearCapturedPayloads(SequencerDomainState::PendingApply& pending) {
    pending.patternGraph.reset();
    pending.activeTrackGraph.reset();
    pending.patternCcLanes.reset();
    pending.activeTrackCcLanes.reset();
    pending.patternCcLaneRevision = 0;
    for (auto& graph : pending.bankGraphs) {
        graph.reset();
    }
    for (auto& lanes : pending.bankCcLanes) {
        lanes.reset();
    }
    pending.bankCcLaneRevisions.fill(0);
}

FLASHMEM bool prepareActiveTrackPayload(
    const sequencer::SequencerPatternState& source,
    GraphPtr& preparedGraph,
    CcLanePtr& preparedCcLanes
) {
    preparedGraph.reset();
    preparedCcLanes.reset();
    if (source.graph) {
        preparedGraph = core::app::makeExtmemUnique<StepSequencerGraph>(*source.graph);
        if (!preparedGraph) return false;
    }

    return sequencer::cloneSequencerCcLaneBank(
        preparedCcLanes,
        sequencer::sequencerCcLaneView(source)
    );
}

FLASHMEM void installPreparedActiveTrack(
    sequencer::SequencerTrackBankState& bank,
    const sequencer::SequencerState& editor,
    GraphPtr& graph,
    CcLanePtr& ccLanes
) {
    auto& activeTrack = bank.track(bank.activeTrackIndex());
    sequencer::SequencerPatternSnapshot snapshot{};
    sequencer::captureSnapshot(editor.pattern, snapshot);
    sequencer::installTrackContentSnapshotWithOwnedPayload(
        activeTrack,
        snapshot,
        std::move(graph),
        std::move(ccLanes)
    );
    activeTrack.ccLaneRevision.set(editor.pattern.ccLaneRevision.get());
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
    state.macroUi.resetInteraction();
    state.macroUi.resetProjectRuntime();
    state.trackNavigation.reset();
}

FLASHMEM void CoreStateLifecycle::resetSequencerDomain_(CoreState& state) {
    state.sequencer.reset();
    state.sequencerTracks.reset();
    if (!sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to initialize sequencer track bank");
    }
    if (state.sequencerDomain_.pendingApply) {
        state.sequencerDomain_.pendingApply->valid = false;
        clearCapturedPayloads(*state.sequencerDomain_.pendingApply);
    }
    state.requestSequencerRuntimeProjectReset();
}

FLASHMEM void CoreStateLifecycle::resetUiState_(CoreState& state) {
    state.viewSelector.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.patternPitchSettings.reset();
    state.dataManager.resetSession(DataManagerContext::MACRO);
    state.dataManager.feedback.set("");
    // Factory reset already cleared Project-scoped Macro runtime in
    // resetMacroDomain_. This second pass owns UI/session state only.
    state.macroUi.resetInteraction();
    state.projectNavigation.reset();
    state.projectTrackEditor.reset();
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
    state.updateMacroValueHistoryCoalescing(nowMs);
    updateMutationCoalescers_(state);
    updatePendingSharedTrackPersist_(state);
}

FLASHMEM void CoreStateLifecycle::flush(CoreState& state) {
    state.flushMacroValueHistoryCoalescing();
    state.projectSettingsHistory.endCoalescing();
    flushMutationCoalescers_(state);
    flushPendingSharedTrackPersist_(state);
}

FLASHMEM void CoreStateLifecycle::flushProjectMutationCoalescing(CoreState& state) {
    state.flushMacroValueHistoryCoalescing();
    state.projectSettingsHistory.endCoalescing();
    flushMutationCoalescers_(state);
}

FLASHMEM void CoreStateLifecycle::resetStandaloneTransientUi(CoreState& state) {
    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::RESET
        );
        return;
    }
    state.macroEdit.reset();
    state.macroUi.resetInteraction();
    reprojectActiveMacroManualOverrides(state);
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    state.structureClipboard.clear();
    state.sequencer.stepEdit.visible.set(false);
    state.sequencer.stepEdit.reset();
    state.sequencer.patternEditor.reset();
    state.sequencer.contextSelector.reset();
    state.sequencer.stepPresetPicker.reset();
    state.sequencer.stepPropertyInlineSelector.reset();
    state.sequencer.patternQuickControls.reset();
    state.sequencer.structureUi.reset();
    state.projectNavigation.reset();
    state.projectTrackEditor.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.dataManager.resetSession(DataManagerContext::MACRO);
}

FLASHMEM void CoreStateLifecycle::resetMusicalProject(CoreState& state) {
    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::RESET
        );
        return;
    }
    const bool historyBoundaryCleared = state.clearProjectHistory();
    state.project.reset();
    state.projectTracks.reset();
    state.pages.initDefaults();

    state.sequencer.reset();
    state.sequencerTracks.reset();
    if (!sequencer::initializeTrackBankFromActive(state.sequencerTracks, state.sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to initialize sequencer track bank");
    }
    clearPendingSequencerApply(state);
    state.requestSequencerRuntimeProjectReset();

    state.setSharedTrackState_(macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK, 0, false);
    macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.statusBar.tempo.set(120.0f);
    if (!state.statusBar.tempoLocked.get()) {
        state.statusBar.tempoDisplay.set(120.0f);
    }
    state.statusBar.pageName.set(state.pages.activePageData().name);

    state.macroEdit.reset();
    state.macroUi.resetInteraction();
    state.macroUi.resetProjectRuntime();
    state.requestMacroRuntimeOwnerActivation();
    state.trackNavigation.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    state.structureClipboard.clear();
    state.sequencer.stepEdit.visible.set(false);
    state.sequencer.stepEdit.reset();
    state.sequencer.patternEditor.reset();
    state.sequencer.contextSelector.reset();
    state.sequencer.stepPresetPicker.reset();
    state.sequencer.stepPropertyInlineSelector.reset();
    state.sequencer.patternQuickControls.reset();
    state.sequencer.structureUi.reset();
    state.deviceSettings.reset();
    state.sequencerSettings.reset();
    state.patternPitchSettings.reset();
    state.projectNavigation.reset();
    state.projectTrackEditor.reset();

    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));

    if (!historyBoundaryCleared) {
        // A full Project replacement no longer needs rollback facts. If the
        // transient pair was already inconsistent, discard every stale owner
        // only after the replacement has made the old graph unreachable.
        OC_LOG_ERROR(
            "[CoreState] Invalid Modulator audition discarded by Project reset"
        );
        state.sequencerDomain_.coalescedPatternHistory.clear();
        state.macroHistory.clear();
        state.sequencerHistory.clear();
        state.projectTrackHistory.clear();
        state.projectHistory.clear();
    }

    flushMutationCoalescers_(state);
}

FLASHMEM void CoreStateLifecycle::factoryReset(CoreState& state) {
    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::RESET
        );
        return;
    }
    const auto resetStatus = state.settings.factoryResetStatus();
    if (resetStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] CoreSettings factory reset failed: {}",
                    persistence::persistenceWriteStatusLabel(resetStatus));
    }
    const bool historyBoundaryCleared = state.clearProjectHistory();
    resetMacroDomain_(state);
    state.requestMacroRuntimeOwnerActivation();
    resetSequencerDomain_(state);
    state.project.reset();
    state.projectTracks.reset();
    resetUiState_(state);
    state.sharedTrackPersistPending_ = false;
    state.sharedTrackPersistTimestampMs_ = 0;
    if (!historyBoundaryCleared) {
        OC_LOG_ERROR(
            "[CoreState] Invalid Modulator audition discarded by factory reset"
        );
        state.sequencerDomain_.coalescedPatternHistory.clear();
        state.macroHistory.clear();
        state.sequencerHistory.clear();
        state.projectTrackHistory.clear();
        state.projectHistory.clear();
    }
    persistFactoryDefaults_(state);
}

FLASHMEM bool CoreStateLifecycle::queuePendingSequencerApply(
    CoreState& state,
    sequencer::SequencerState& staged,
    bool merge
) {
    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        return false;
    }
    if (!state.sequencerDomain_.pendingApply) return false;

    GraphPtr activeTrackGraph;
    CcLanePtr activeTrackCcLanes;
    if (!prepareActiveTrackPayload(
            staged.pattern,
            activeTrackGraph,
            activeTrackCcLanes
        )) {
        return false;
    }

    auto& pending = *state.sequencerDomain_.pendingApply;
    clearCapturedPayloads(pending);
    sequencer::captureSnapshot(staged.pattern, pending.snapshot);
    pending.patternCcLaneRevision = staged.pattern.ccLaneRevision.get();
    pending.patternGraph = std::move(staged.pattern.graph);
    pending.patternCcLanes = std::move(staged.pattern.ccLanes);
    pending.activeTrackGraph = std::move(activeTrackGraph);
    pending.activeTrackCcLanes = std::move(activeTrackCcLanes);
    pending.anchorPlayhead = state.sequencer.playheadStep.get();
    pending.merge = merge;
    pending.fullBank = false;
    pending.valid = true;
    return true;
}

FLASHMEM bool CoreStateLifecycle::queuePendingSequencerBankApply(
    CoreState& state,
    sequencer::SequencerTrackBankState& stagedBank,
    sequencer::SequencerState& staged
) {
    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        return false;
    }
    if (!state.sequencerDomain_.pendingApply) return false;
    auto& pending = *state.sequencerDomain_.pendingApply;
    clearCapturedPayloads(pending);
    sequencer::captureTrackBankSnapshot(
        stagedBank,
        staged,
        pending.bankSnapshot
    );
    takeTrackBankPayloads(pending, stagedBank);
    pending.patternCcLaneRevision = staged.pattern.ccLaneRevision.get();
    pending.patternGraph = std::move(staged.pattern.graph);
    pending.patternCcLanes = std::move(staged.pattern.ccLanes);
    pending.anchorPlayhead = state.sequencer.playheadStep.get();
    pending.merge = false;
    pending.fullBank = true;
    pending.valid = true;
    return true;
}

FLASHMEM void CoreStateLifecycle::clearPendingSequencerApply(CoreState& state) {
    if (!state.sequencerDomain_.pendingApply) return;
    state.sequencerDomain_.pendingApply->valid = false;
    state.sequencerDomain_.pendingApply->fullBank = false;
    clearCapturedPayloads(*state.sequencerDomain_.pendingApply);
}

void CoreStateLifecycle::applyPendingSequencerApplyIfReady(CoreState& state) {
    if (!state.sequencerDomain_.pendingApply || !state.sequencerDomain_.pendingApply->valid) return;

    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            sequencer::SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        return;
    }

    if (state.statusBar.playing.get()) {
        const int16_t playhead = state.sequencer.playheadStep.get();
        if (playhead < 0) return;
        if (playhead == state.sequencerDomain_.pendingApply->anchorPlayhead) return;
    }

    if (!state.clearSequencerHistory()) {
        OC_LOG_ERROR(
            "[CoreState] Pending Sequencer load cancelled: invalid Project transaction"
        );
        clearPendingSequencerApply(state);
        return;
    }

    if (state.sequencerDomain_.pendingApply->fullBank) {
        sequencer::applyTrackBankSnapshot(
            state.sequencerTracks,
            state.sequencer,
            state.sequencerDomain_.pendingApply->bankSnapshot
        );
        applyTrackBankPayloads(state.sequencerTracks, *state.sequencerDomain_.pendingApply);
        installCapturedGraph(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternGraph,
            state.sequencerDomain_.pendingApply
                ->bankSnapshot
                .tracks[state.sequencerDomain_.pendingApply->bankSnapshot.activeTrack]
                .graphRevision
        );
        installCapturedCcLanes(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternCcLanes,
            state.sequencerDomain_.pendingApply->patternCcLaneRevision
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
        installCapturedCcLanes(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternCcLanes,
            state.sequencerDomain_.pendingApply->patternCcLaneRevision
        );
        installPreparedActiveTrack(
            state.sequencerTracks,
            state.sequencer,
            state.sequencerDomain_.pendingApply->activeTrackGraph,
            state.sequencerDomain_.pendingApply->activeTrackCcLanes
        );
    } else {
        sequencer::applySnapshotToEditor(state.sequencer, state.sequencerDomain_.pendingApply->snapshot);
        installCapturedGraph(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternGraph,
            state.sequencerDomain_.pendingApply->snapshot.graphRevision
        );
        installCapturedCcLanes(
            state.sequencer.pattern,
            state.sequencerDomain_.pendingApply->patternCcLanes,
            state.sequencerDomain_.pendingApply->patternCcLaneRevision
        );
        installPreparedActiveTrack(
            state.sequencerTracks,
            state.sequencer,
            state.sequencerDomain_.pendingApply->activeTrackGraph,
            state.sequencerDomain_.pendingApply->activeTrackCcLanes
        );
    }
    state.markProjectMutated();
    state.refreshSharedTrackStateFromSequencer();
    state.sequencerDomain_.pendingApply->valid = false;
    clearCapturedPayloads(*state.sequencerDomain_.pendingApply);
}

}  // namespace core::state
