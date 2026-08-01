#include "state/CoreState.hpp"

#include <new>
#include <cstdio>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/NotificationQueue.hpp>
#include <oc/time/Time.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"
#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::state {

namespace {

FLASHMEM int32_t sequencerHistoryValueForProperty(
    const sequencer::SequencerHistoryPatternSnapshot& snapshot,
    uint8_t step,
    sequencer::StepProperty property
) {
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) {
        return 0;
    }

    switch (property) {
        case sequencer::StepProperty::NOTE:
            return snapshot.flat.note[step];
        case sequencer::StepProperty::VELOCITY:
            return snapshot.flat.velocity[step];
        case sequencer::StepProperty::GATE:
            return snapshot.flat.gate[step];
        case sequencer::StepProperty::NUDGE:
            return snapshot.flat.nudge[step];
        case sequencer::StepProperty::PROBABILITY:
            return snapshot.flat.probability[step];
        default:
            return 0;
    }
}

FLASHMEM sequencer::SequencerHistoryDescriptor makeStepPropertyHistoryDescriptor(
    uint8_t track,
    uint8_t step,
    sequencer::StepProperty property,
    const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after
) {
    const int32_t beforeValue = sequencerHistoryValueForProperty(before, step, property);
    const int32_t afterValue = sequencerHistoryValueForProperty(after, step, property);
    if (beforeValue == afterValue) {
        return sequencer::SequencerHistoryDescriptor{
            .kind = sequencer::SequencerHistoryActionKind::StepEdit,
            .trackIndex = track,
            .stepIndex = step,
            .property = property,
            .hasValue = false,
        };
    }

    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepPropertyEdit,
        .trackIndex = track,
        .stepIndex = step,
        .property = property,
        .hasValue = true,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

FLASHMEM sequencer::SequencerHistoryDescriptor makeStepStateHistoryDescriptor(
    uint8_t track,
    uint8_t step,
    const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after
) {
    const bool beforeEnabled = before.flat.enabledMask.test(step);
    const bool afterEnabled = after.flat.enabledMask.test(step);
    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepToggle,
        .trackIndex = track,
        .stepIndex = step,
        .property = sequencer::StepProperty::NOTE,
        .hasValue = beforeEnabled != afterEnabled,
        .beforeValue = beforeEnabled ? 1 : 0,
        .afterValue = afterEnabled ? 1 : 0,
    };
}

}  // namespace

FLASHMEM void SequencerDomainState::CoalescedPatternHistory::clear() {
    pending = false;
    kind = Kind::StepProperty;
    activeTrack = 0;
    step = 0;
    property = sequencer::StepProperty::NOTE;
    stateProperty = false;
    lane = sequencer::SequencerHistoryDescriptor::INVALID_INDEX;
    lastTouchedMs = 0;
    payloadPlan = sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly;
    sealed = false;
    hasChange = false;
    prospectiveGraphInstalled = false;
    genericMutationPendingAtBegin = false;
    preparedStepChange.reset();
    synchronization.reset();
    preparedCcLaneChange.reset();
}

FLASHMEM void CoreState::consumePendingSequencerMutation_(bool* priorMutation) {
    auto* coalescer = sequencerDomain_.mutationCoalescer.get();
    if (coalescer == nullptr) return;

    const bool hadArmedMutation =
        priorMutation != nullptr && coalescer->hasPendingChanges();
    std::size_t queuedBefore = 0U;
    if (priorMutation != nullptr) {
        queuedBefore = oc::state::NotificationQueue::instance().pendingCount();
    }

    coalescer->consumePendingChangesWithoutAction();

    if (priorMutation != nullptr) {
        const bool hadQueuedMutation =
            oc::state::NotificationQueue::instance().pendingCount() < queuedBefore;
        *priorMutation = *priorMutation || hadArmedMutation || hadQueuedMutation;
    }
}

FLASHMEM CoreState::SequencerPatternHistoryCommitOutcome
CoreState::abandonUnsafeSequencerPatternHistory_(const char* reason) {
    OC_LOG_ERROR(
        "[CoreState] Coalesced Sequencer history unavailable ({}); "
        "clearing Project history boundary",
        reason
    );
    if (!clearProjectHistory()) {
        OC_LOG_ERROR("[CoreState] Failed to close Project history boundary");
    }
    return SequencerPatternHistoryCommitOutcome::Failed;
}

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternSnapshot before,
    sequencer::SequencerHistoryPatternSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor,
    sequencer::SequencerHistoryPatternStorage storage
) {
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    uint8_t targetTrack = activeTrack;
    if (descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = activeTrack;
    } else {
        targetTrack = sequencer::SequencerTrackBankState::clampTrackIndex(descriptor.trackIndex);
        descriptor.trackIndex = targetTrack;
    }

    const bool recorded = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencerHistory.recordFlatPattern(
              targetTrack,
              std::move(before),
              std::move(after),
              descriptor
          )
        : sequencerHistory.recordPattern(
              targetTrack,
              std::move(before),
              std::move(after),
              descriptor
          );
    if (!recorded) {
        return false;
    }

    const bool synchronized = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
        : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternChangePtr change
) {
    if (!change) return false;

    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const uint8_t targetTrack =
        change->descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX
            ? activeTrack
            : sequencer::SequencerTrackBankState::clampTrackIndex(
                  change->descriptor.trackIndex
              );
    change->trackIndex = targetTrack;
    change->descriptor.trackIndex = targetTrack;
    const auto storage = change->storage;
    if (!sequencerHistory.recordPattern(std::move(change))) return false;

    const bool synchronized = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
        : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryTrackBankSnapshot before,
    sequencer::SequencerHistoryTrackBankSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor
) {
    if (!sequencerHistory.recordFullBank(
            std::move(before),
            std::move(after),
            descriptor
        )) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (!sequencerHistory.recordFullBank(std::move(change))) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::canRecordSequencerBankHistory(
    const sequencer::SequencerHistoryFullBankChange& change
) const {
    // installTrackBankState rejects the complete bank install while a Step Draft
    // is active, including content-only changes with unchanged topology. Keep
    // admission identical to that live-write guard so History can never publish
    // an `after` snapshot that was not installed.
    return !sequencer.stepContentDraft.active.get() &&
        sequencerHistory.canRecordFullBank(change);
}

FLASHMEM void CoreState::recordPreparedSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (!change || !canRecordSequencerBankHistory(*change)) return;
    const uint16_t enabledMask = change->after.flat.enabledMask;
    const uint8_t activeTrack = change->after.flat.activeTrack;
    if (!publishPreparedSequencerTrackState(enabledMask, activeTrack)) return;
    sequencerHistory.recordPreparedFullBank(std::move(change));
    publishPreparedSequencerMutation();
}

FLASHMEM bool CoreState::recordSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (!sequencerHistory.recordStructure(std::move(change))) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::canRecordSequencerStructureHistory(
    const sequencer::SequencerHistoryTrackStructureChange& change
) const {
    return !sequencer.stepContentDraft.active.get() &&
        sequencerHistory.canRecordStructure(change);
}

FLASHMEM void CoreState::recordPreparedSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (!change || !canRecordSequencerStructureHistory(*change)) return;
    const uint16_t enabledMask = change->after.enabledMask;
    const uint8_t activeTrack = change->after.activeTrack;
    if (!publishPreparedSequencerTrackState(enabledMask, activeTrack)) return;
    sequencerHistory.recordPreparedStructure(std::move(change));
    publishPreparedSequencerMutation();
}

FLASHMEM void CoreState::publishPreparedSequencerMutation() {
    // The prepared transaction already performed the coalescer action's
    // editor-to-bank synchronization. Cancel only this coalescer's queued
    // callbacks (including later entries in an active notification wave)
    // and consume an already-armed mark before publishing directly.
    consumePendingSequencerMutation_();
    markProjectMutated();
}

FLASHMEM bool CoreState::beginOrContinueSequencerPatternHistoryCoalescing(
    uint8_t step,
    sequencer::StepProperty property,
    uint32_t nowMs,
    sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
    bool stateProperty
) {
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) {
        return false;
    }

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();

    if (pending.matchesStepProperty(
            activeTrack,
            step,
            property,
            stateProperty
        )) {
        // The stable grouping key intentionally excludes storage policy. A
        // plan drift is a caller-classification bug; reject it atomically
        // rather than splitting one 500 ms gesture into two Undo entries.
        if (pending.payloadPlan != payloadPlan ||
            !pending.sealed || !pending.preparedStepChange ||
            !sequencer::preparedActiveTrackSynchronizationMatches(
                sequencerTracks,
                pending.synchronization
            )) {
            return false;
        }
        consumePendingSequencerMutation_(
            &pending.genericMutationPendingAtBegin
        );
        pending.sealed = false;
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) {
            return false;
        }
    }

    sequencer::SequencerHistoryGraphPtr prospectiveGraph;
    auto change = sequencer::prepareHistoryPatternChangeBefore(
        sequencerTracks,
        sequencer,
        activeTrack,
        payloadPlan,
        prospectiveGraph
    );
    if (!change ||
        !sequencer::reservePreparedHistoryPatternAfter(
            sequencerTracks,
            sequencer,
            *change,
            payloadPlan
        )) {
        return false;
    }

    sequencer::SequencerPreparedActiveTrackSynchronization synchronization;
    if (!sequencer::reservePreparedActiveTrackSynchronization(
            sequencerTracks,
            sequencer,
            activeTrack,
            payloadPlan,
            synchronization
        ) ||
        activeTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(
            sequencerTracks,
            synchronization
        )) {
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::StepProperty;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.property = property;
    pending.stateProperty = stateProperty;
    pending.lastTouchedMs = nowMs;
    pending.payloadPlan = payloadPlan;
    pending.sealed = false;
    pending.hasChange = false;
    pending.preparedStepChange = std::move(change);
    pending.synchronization = std::move(synchronization);

    if (prospectiveGraph) {
        if (sequencer.pattern.graph) {
            pending.clear();
            return false;
        }
        sequencer.pattern.graph = std::move(prospectiveGraph);
        pending.prospectiveGraphInstalled = true;
    }
    // Preparation is now irrevocably successful but the caller has not yet
    // performed its live mutation. Isolate any earlier generic Sequencer mark
    // (including a still-queued callback) so Step publication can subsume it,
    // or restore it if this gesture later proves to be a no-op/net return.
    consumePendingSequencerMutation_(&pending.genericMutationPendingAtBegin);
    return true;
}

FLASHMEM bool CoreState::sealSequencerPatternHistoryCoalescing(
    bool mutationChanged
) {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending ||
        pending.kind != SequencerDomainState::CoalescedPatternHistory::Kind::StepProperty ||
        pending.sealed ||
        !pending.preparedStepChange ||
        pending.activeTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(
            sequencerTracks,
            pending.synchronization
        )) {
        return false;
    }

    if (!mutationChanged) {
        if (pending.hasChange) {
            // A prior changed seal owns the generic mutation mark and will
            // publish it at the prepared 500 ms boundary.
            consumePendingSequencerMutation_();
            pending.sealed = true;
            return true;
        }

        // A virgin no-op did not create an owned musical mutation. Preserve
        // any generic Sequencer mark/callback that predates this transaction.
        if (pending.genericMutationPendingAtBegin) {
            auto* coalescer = sequencerDomain_.mutationCoalescer.get();
            if (coalescer != nullptr) coalescer->markChanged();
        }
        if (pending.prospectiveGraphInstalled &&
            sequencer.pattern.graph &&
            !sequencer.pattern.graph->enabled &&
            sequencer.pattern.graphRevision.get() ==
                pending.preparedStepChange->before.flat.graphRevision) {
            sequencer.pattern.graph.reset();
        }
        pending.clear();
        return true;
    }

    auto& change = *pending.preparedStepChange;
    if (!sequencer::capturePreparedHistoryPatternAfterUsingReservedStorage(
            sequencerTracks,
            sequencer,
            change
        ) ||
        !sequencer::refreshPreparedActiveTrackSynchronizationUsingReservedStorage(
            sequencerTracks,
            sequencer,
            pending.synchronization
        )) {
        return false;
    }

    change.descriptor = pending.stateProperty
        ? makeStepStateHistoryDescriptor(
              pending.activeTrack,
              pending.step,
              change.before,
              change.after
          )
        : makeStepPropertyHistoryDescriptor(
              pending.activeTrack,
              pending.step,
              pending.property,
              change.before,
              change.after
          );

    if (sequencer::sameMusicalHistorySnapshot(change.before, change.after)) {
        // The musical bytes returned to Before, but setters may have advanced
        // editor-only revision counters on the round trip. The generic
        // coalescer is intentionally cancelled below, so restore those exact
        // counters and keep the still-unpublished bank byte-coherent. A Graph
        // created prospectively for this session is not part of Before and
        // must not survive an otherwise exact net return.
        if (pending.prospectiveGraphInstalled &&
            !change.before.graph &&
            sequencer.pattern.graph) {
            sequencer.pattern.graph.reset();
        }
        sequencer::synchronizeHistoryPatternRevisionSignals(
            sequencer.pattern,
            change.before.flat,
            change.before.ccLaneRevision
        );
        // Cancel callbacks from the musical round trip. If the generic
        // coalescer already owned an earlier mutation, re-arm that independent
        // obligation after cancellation; a changed prepared commit would have
        // subsumed it, but this net-zero transaction publishes nothing.
        const bool restoreGenericMutation =
            pending.genericMutationPendingAtBegin;
        consumePendingSequencerMutation_();
        if (restoreGenericMutation) {
            auto* coalescer = sequencerDomain_.mutationCoalescer.get();
            if (coalescer != nullptr) coalescer->markChanged();
        }
        pending.clear();
        return true;
    }
    if (!sequencerHistory.canRecordPattern(change)) {
        return false;
    }

    consumePendingSequencerMutation_();
    pending.hasChange = true;
    pending.sealed = true;
    return true;
}

FLASHMEM bool CoreState::beginOrContinueSequencerCcLaneEventHistoryCoalescing(
    uint8_t lane,
    uint8_t step,
    int32_t beforeValue,
    int32_t afterValue,
    const sequencer::SequencerCcLaneBank* afterBank,
    uint32_t nowMs
) {
    if (lane >= sequencer::SequencerCcLaneBank::MAX_LANES ||
        step >= sequencer::SequencerCcLaneBank::MAX_STEPS ||
        beforeValue < -1 || beforeValue > 127 ||
        afterValue < 0 || afterValue > 127 || afterBank == nullptr ||
        !afterBank->lanes[lane].occupied ||
        !afterBank->lanes[lane].activeMask.test(step) ||
        afterBank->lanes[lane].values[step] != afterValue) {
        return false;
    }

    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const auto captureAfter = [this, afterBank](
                                  sequencer::SequencerHistoryPatternChange& change
                              ) {
        if (!sequencer::captureHistorySnapshotUsingReservedGraph(
                sequencer,
                change.after
            ) ||
            !sequencer::captureSequencerCcLaneBankUsingReservedStorage(
                afterBank,
                change.after.ccLanes
            )) {
            return false;
        }
        change.after.ccLanesCaptured = true;
        return true;
    };

    if (pending.matchesCcLaneEvent(activeTrack, lane, step)) {
        auto* change = pending.preparedCcLaneChange.get();
        if (change == nullptr || !captureAfter(*change)) {
            if (change != nullptr) {
                (void)sequencer::applyHistorySnapshotToEditor(
                    sequencer,
                    change->before
                );
            }
            pending.clear();
            return false;
        }
        change->descriptor.afterValue = afterValue;
        const bool noChange = sequencer::sameMusicalHistorySnapshot(
            change->before,
            change->after
        );
        if (!noChange && !sequencerHistory.canRecordPattern(*change)) {
            (void)sequencer::applyHistorySnapshotToEditor(
                sequencer,
                change->before
            );
            pending.clear();
            return false;
        }
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        const auto outcome = commitSequencerPatternHistoryCoalescing_();
        if (outcome == SequencerPatternHistoryCommitOutcome::Failed) {
            return false;
        }
    }

    auto change = core::app::makeExtmemUnique<
        sequencer::SequencerHistoryPatternChange
    >();
    if (!change) {
        pending.clear();
        return false;
    }
    change->trackIndex = activeTrack;
    change->storage = sequencer::SequencerHistoryPatternStorage::FullGraph;
    change->descriptor = {
        .kind = sequencer::SequencerHistoryActionKind::CcLaneEventEdit,
        .trackIndex = activeTrack,
        .laneIndex = lane,
        .stepIndex = step,
        .hasValue = true,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
    if (!sequencer::captureHistorySnapshot(sequencer, change->before) ||
        !captureAfter(*change) ||
        !sequencerHistory.canRecordPattern(*change)) {
        pending.clear();
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.kind = SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.lane = lane;
    pending.lastTouchedMs = nowMs;
    pending.preparedCcLaneChange = std::move(change);
    return true;
}

FLASHMEM CoreState::SequencerPatternHistoryCommitOutcome
CoreState::commitSequencerPatternHistoryCoalescing_() {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return SequencerPatternHistoryCommitOutcome::NoPending;
    }

    const uint8_t targetTrack = pending.activeTrack;
    const bool ccLaneEvent = pending.kind ==
        SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent;
    if (ccLaneEvent) {
        auto change = std::move(pending.preparedCcLaneChange);
        if (!change) {
            pending.clear();
            return abandonUnsafeSequencerPatternHistory_(
                "prepared CC Lane entry missing"
            );
        }
        if (sequencer::sameMusicalHistorySnapshot(
                change->before,
                change->after
            )) {
            pending.clear();
            return SequencerPatternHistoryCommitOutcome::NoChange;
        }
        if (!sequencerHistory.canRecordPattern(*change)) {
            const bool restored = sequencer::applyHistorySnapshotToTrack(
                sequencerTracks,
                sequencer,
                targetTrack,
                change->before
            );
            pending.clear();
            return restored
                ? SequencerPatternHistoryCommitOutcome::Failed
                : abandonUnsafeSequencerPatternHistory_(
                      "CC Lane rollback failed"
                  );
        }

        pending.clear();
        sequencerHistory.recordPreparedPattern(std::move(change));
        markSequencerProjectMutated_();
        return SequencerPatternHistoryCommitOutcome::Committed;
    }

    if (!pending.sealed || !pending.preparedStepChange ||
        targetTrack != sequencerTracks.activeTrackIndex() ||
        !sequencer::preparedActiveTrackSynchronizationMatches(
            sequencerTracks,
            pending.synchronization
        )) {
        return SequencerPatternHistoryCommitOutcome::Failed;
    }

    auto change = std::move(pending.preparedStepChange);
    auto synchronization = std::move(pending.synchronization);
    pending.clear();

    sequencer::publishPreparedActiveTrackSynchronization(
        sequencerTracks,
        sequencer,
        change->after,
        std::move(synchronization)
    );
    sequencerHistory.recordPreparedPattern(std::move(change));
    publishPreparedSequencerMutation();
    (void)refreshSharedTrackStateFromSequencer();
    return SequencerPatternHistoryCommitOutcome::Committed;
}

FLASHMEM bool CoreState::commitSequencerPatternHistoryCoalescing() {
    return commitSequencerPatternHistoryCoalescing_() ==
        SequencerPatternHistoryCommitOutcome::Committed;
}

FLASHMEM bool CoreState::updateSequencerPatternHistoryCoalescing(uint32_t nowMs) {
    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return false;
    }

    const uint32_t idleMs = pending.kind ==
            SequencerDomainState::CoalescedPatternHistory::Kind::CcLaneEvent
        ? SequencerDomainState::COALESCED_CC_LANE_HISTORY_IDLE_MS
        : SequencerDomainState::COALESCED_PATTERN_HISTORY_IDLE_MS;
    if (static_cast<uint32_t>(nowMs - pending.lastTouchedMs) < idleMs) {
        return false;
    }

    return commitSequencerPatternHistoryCoalescing();
}

bool CoreState::hasPendingSequencerPatternHistoryCoalescing() const {
    return sequencerDomain_.coalescedPatternHistory.pending;
}

}  // namespace core::state
