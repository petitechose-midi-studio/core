#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"

namespace core::state::sequencer {

struct SequencerTrackActivationQueueTestAccess {
    static void seedIdentifiers(
        SequencerTrackActivationQueue& queue,
        uint32_t generation,
        uint32_t operationId
    ) {
        queue.next_generation_ = generation;
        queue.next_operation_id_ = operationId;
    }
};

}  // namespace core::state::sequencer

namespace {

using core::state::sequencer::SequencerTrackActivationBatch;
using core::state::sequencer::SequencerTrackActivationEntrySnapshot;
using core::state::sequencer::SequencerTrackActivationExpectedState;
using core::state::sequencer::SequencerTrackActivationHistoryTransition;
using core::state::sequencer::SequencerTrackActivationHistoryTransitionPlan;
using core::state::sequencer::SequencerTrackActivationMutationGuard;
using core::state::sequencer::SequencerTrackActivationOrigin;
using core::state::sequencer::SequencerTrackActivationPlan;
using core::state::sequencer::SequencerTrackActivationQueue;
using core::state::sequencer::SequencerTrackActivationQueueTestAccess;
using core::state::sequencer::SequencerTrackActivationRealtimeView;
using core::state::sequencer::SequencerTrackActivationRuntimePublication;
using core::state::sequencer::SequencerTrackActivationStatus;
using core::state::sequencer::SequencerTrackActivationTelemetry;
using core::state::sequencer::SequencerTrackActivationTarget;
using core::state::sequencer::activationHistoryRef;

struct QueuePublicObservation {
    uint16_t pendingMask = 0;
    uint32_t telemetryRevision = 0;
    std::array<
        SequencerTrackActivationTelemetry,
        SequencerTrackActivationQueue::TRACK_COUNT
    > telemetry{};
    SequencerTrackActivationRuntimePublication publication{};
    std::array<
        SequencerTrackActivationRealtimeView,
        SequencerTrackActivationQueue::TRACK_COUNT
    > realtime{};
};

QueuePublicObservation observePublicState(
    SequencerTrackActivationQueue& queue
) {
    QueuePublicObservation observed;
    observed.pendingMask = queue.pendingTrackMask();
    observed.telemetryRevision = queue.telemetryRevision().get();
    observed.publication = queue.captureRuntimePublication();
    for (uint8_t track = 0;
         track < SequencerTrackActivationQueue::TRACK_COUNT;
         ++track) {
        observed.telemetry[track] = queue.telemetry(track);
        observed.realtime[track] = queue.realtimeView(track);
    }
    return observed;
}

void assertEntrySnapshotEqual(
    const SequencerTrackActivationEntrySnapshot& lhs,
    const SequencerTrackActivationEntrySnapshot& rhs
) {
    assert(lhs.phase == rhs.phase);
    assert(lhs.requiresLocalLoopBoundary == rhs.requiresLocalLoopBoundary);
    assert(lhs.target == rhs.target);
    assert(lhs.origin == rhs.origin);
    assert(lhs.generation == rhs.generation);
    assert(lhs.operationId == rhs.operationId);
}

void assertExpectedStateEqual(
    const SequencerTrackActivationExpectedState& lhs,
    const SequencerTrackActivationExpectedState& rhs
) {
    assert(lhs.nextGeneration == rhs.nextGeneration);
    assert(lhs.nextOperationId == rhs.nextOperationId);
    assert(lhs.telemetryRevision == rhs.telemetryRevision);
    for (uint8_t track = 0;
         track < SequencerTrackActivationQueue::TRACK_COUNT;
         ++track) {
        assertEntrySnapshotEqual(lhs.entries[track], rhs.entries[track]);
    }
}

void assertPublicStateEqual(
    const QueuePublicObservation& lhs,
    const QueuePublicObservation& rhs
) {
    assert(lhs.pendingMask == rhs.pendingMask);
    assert(lhs.telemetryRevision == rhs.telemetryRevision);
    assert(lhs.publication.queuedMask == rhs.publication.queuedMask);
    assert(lhs.publication.cancelledMask == rhs.publication.cancelledMask);
    for (uint8_t track = 0;
         track < SequencerTrackActivationQueue::TRACK_COUNT;
         ++track) {
        assert(lhs.telemetry[track].status == rhs.telemetry[track].status);
        assert(lhs.telemetry[track].generation ==
               rhs.telemetry[track].generation);
        assert(lhs.telemetry[track].origin == rhs.telemetry[track].origin);
        assert(lhs.publication.generations[track] ==
               rhs.publication.generations[track]);
        assert(lhs.realtime[track].disposition ==
               rhs.realtime[track].disposition);
        assert(lhs.realtime[track].generation ==
               rhs.realtime[track].generation);
        assert(lhs.realtime[track].requiresLocalLoopBoundary ==
               rhs.realtime[track].requiresLocalLoopBoundary);
    }
}

void publishRuntimeGeneration(SequencerTrackActivationQueue& queue) {
    const auto publication = queue.captureRuntimePublication();
    queue.applyRuntimePublication(publication);
}

uint32_t applyPendingGeneration(
    SequencerTrackActivationQueue& queue,
    uint8_t track
) {
    publishRuntimeGeneration(queue);
    const auto realtime = queue.realtimeView(track);
    assert(realtime.disposition ==
           SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(queue.markAppliedFromRealtime(track, realtime.generation));
    queue.publishRealtimeTelemetry();
    return realtime.generation;
}

SequencerTrackActivationBatch stageOperation(
    SequencerTrackActivationQueue& queue,
    uint16_t trackMask,
    uint16_t appliedMask
) {
    SequencerTrackActivationBatch batch;
    assert(queue.prepare(
        trackMask,
        trackMask,
        true,
        batch,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(queue.armPrepared(batch));
    queue.publishPrepared(batch);
    publishRuntimeGeneration(queue);
    for (uint8_t track = 0;
         track < SequencerTrackActivationQueue::TRACK_COUNT;
         ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((appliedMask & bit) != 0) {
            assert(queue.markAppliedFromRealtime(track, batch.generation));
        }
    }
    if (appliedMask != 0) assert(queue.publishRealtimeTelemetry());
    return batch;
}

void test_pure_normal_plans_preserve_state_and_boundary_masks() {
    SequencerTrackActivationQueue queue;
    const auto before = observePublicState(queue);

    SequencerTrackActivationPlan playing;
    assert(queue.planActivation(
        0x0003,
        0x0001,
        true,
        playing,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(playing.valid());
    assert(playing.batch.localLoopBoundaryMask == 0x0001);
    assert(playing.batch.generation == 1);
    assert(playing.batch.operationId == 1);

    SequencerTrackActivationPlan stopped;
    assert(queue.planActivation(
        0x0003,
        0x0001,
        false,
        stopped,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(stopped.batch.localLoopBoundaryMask == 0);
    assert(stopped.batch.generation == playing.batch.generation);
    assert(stopped.batch.operationId == playing.batch.operationId);
    assertExpectedStateEqual(playing.expected, stopped.expected);

    // Models arbitrary future owner allocation between planning and arming.
    std::vector<uint8_t> detachedOwner(4096, 0x5A);
    assert(detachedOwner.front() == 0x5A);

    const auto after = observePublicState(queue);
    assertPublicStateEqual(before, after);
    SequencerTrackActivationPlan repeated;
    assert(queue.planActivation(0x0003, 0x0001, true, repeated));
    assertExpectedStateEqual(playing.expected, repeated.expected);

    std::cout
        << "[PASS] test_pure_normal_plans_preserve_state_and_boundary_masks\n";
}

void test_planned_normal_arm_is_atomic_and_advances_once() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationPlan plan;
    assert(queue.planActivation(
        0x0003,
        0x0001,
        true,
        plan,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    const uint32_t revisionBefore = queue.telemetryRevision().get();

    std::vector<uint32_t> detachedOwner(1024, plan.batch.operationId);
    assert(detachedOwner.back() == plan.batch.operationId);

    SequencerTrackActivationBatch armed;
    assert(queue.tryArmPlannedActivation(plan, armed));
    assert(armed.trackMask == 0x0003);
    assert(armed.generation == plan.batch.generation);
    assert(armed.operationId == plan.batch.operationId);
    assert(queue.pendingTrackMask() == 0x0003);
    assert(queue.telemetryRevision().get() == revisionBefore);
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.telemetry(1).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.realtimeView(0).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::FROZEN);
    assert(queue.realtimeView(0).requiresLocalLoopBoundary);
    assert(!queue.realtimeView(1).requiresLocalLoopBoundary);

    SequencerTrackActivationPlan stateAfterArm;
    assert(queue.planActivation(0x0004, 0, false, stateAfterArm));
    assert(stateAfterArm.expected.nextGeneration == armed.generation);
    assert(stateAfterArm.expected.nextOperationId == armed.operationId);
    assert(stateAfterArm.expected.entries[0].generation == armed.generation);
    assert(stateAfterArm.expected.entries[0].operationId == armed.operationId);
    assert(stateAfterArm.expected.entries[0].phase != 0);

    const auto publicBeforeRejectedRetry = observePublicState(queue);
    SequencerTrackActivationBatch rejected;
    assert(!queue.tryArmPlannedActivation(plan, rejected));
    assert(!rejected.valid());
    const auto publicAfterRejectedRetry = observePublicState(queue);
    assertPublicStateEqual(
        publicBeforeRejectedRetry,
        publicAfterRejectedRetry
    );
    SequencerTrackActivationPlan stateAfterRejectedRetry;
    assert(queue.planActivation(0x0004, 0, false, stateAfterRejectedRetry));
    assertExpectedStateEqual(
        stateAfterArm.expected,
        stateAfterRejectedRetry.expected
    );

    std::cout
        << "[PASS] test_planned_normal_arm_is_atomic_and_advances_once\n";
}

void test_planned_normal_arm_rejects_stale_counters_without_mutation() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationPlan stale;
    assert(queue.planActivation(0x0001, 0x0001, true, stale));

    // Legacy prepare reserves both counters but intentionally does not touch a
    // slot. This isolates counter staleness from slot and telemetry changes.
    SequencerTrackActivationBatch reservation;
    assert(queue.prepare(0x0002, 0x0002, true, reservation));
    SequencerTrackActivationPlan beforeRejectedArm;
    assert(queue.planActivation(0x0001, 0x0001, true, beforeRejectedArm));
    const auto publicBefore = observePublicState(queue);

    SequencerTrackActivationBatch rejected;
    assert(!queue.tryArmPlannedActivation(stale, rejected));
    assert(!rejected.valid());
    assertPublicStateEqual(publicBefore, observePublicState(queue));
    SequencerTrackActivationPlan afterRejectedArm;
    assert(queue.planActivation(0x0001, 0x0001, true, afterRejectedArm));
    assertExpectedStateEqual(
        beforeRejectedArm.expected,
        afterRejectedArm.expected
    );

    std::cout
        << "[PASS] test_planned_normal_arm_rejects_stale_counters_without_mutation\n";
}

void test_planned_normal_arm_rejects_stale_slots_without_mutation() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch unrelated;
    assert(queue.prepare(0x0001, 0x0001, true, unrelated));
    assert(queue.armPrepared(unrelated));
    queue.publishPrepared(unrelated);

    SequencerTrackActivationPlan stale;
    assert(queue.planActivation(0x0002, 0x0002, true, stale));
    const auto publication = queue.captureRuntimePublication();
    queue.applyRuntimePublication(publication);

    SequencerTrackActivationPlan beforeRejectedArm;
    assert(queue.planActivation(0x0002, 0x0002, true, beforeRejectedArm));
    const auto publicBefore = observePublicState(queue);
    SequencerTrackActivationBatch rejected;
    assert(!queue.tryArmPlannedActivation(stale, rejected));
    assert(!rejected.valid());
    assertPublicStateEqual(publicBefore, observePublicState(queue));
    SequencerTrackActivationPlan afterRejectedArm;
    assert(queue.planActivation(0x0002, 0x0002, true, afterRejectedArm));
    assertExpectedStateEqual(
        beforeRejectedArm.expected,
        afterRejectedArm.expected
    );

    std::cout
        << "[PASS] test_planned_normal_arm_rejects_stale_slots_without_mutation\n";
}

void test_pure_history_plan_and_atomic_arm_cover_queue_and_cancel() {
    SequencerTrackActivationQueue queue;
    const auto source = stageOperation(queue, 0x0003, 0x0001);
    const auto beforePlan = observePublicState(queue);

    SequencerTrackActivationHistoryTransitionPlan playing;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        playing
    ));
    assert(playing.queuedMask == 0x0001);
    assert(playing.cancelledMask == 0x0002);
    assert(playing.localLoopBoundaryMask == 0x0001);
    assert(playing.generation != 0);

    SequencerTrackActivationHistoryTransitionPlan stopped;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        false,
        stopped
    ));
    assert(stopped.localLoopBoundaryMask == 0);
    assert(stopped.generation == playing.generation);
    assertExpectedStateEqual(playing.expected, stopped.expected);
    assertPublicStateEqual(beforePlan, observePublicState(queue));

    std::vector<uint8_t> replayOwners(8192, 0xA5);
    assert(replayOwners.front() == 0xA5);
    const uint32_t revisionBeforeArm = queue.telemetryRevision().get();
    SequencerTrackActivationHistoryTransition transition;
    assert(queue.tryArmPlannedHistoryTransition(playing, transition));
    assert(transition.queuedMask == playing.queuedMask);
    assert(transition.cancelledMask == playing.cancelledMask);
    assertEntrySnapshotEqual(
        transition.previous[0],
        playing.expected.entries[0]
    );
    assertEntrySnapshotEqual(
        transition.previous[1],
        playing.expected.entries[1]
    );
    assert(queue.telemetryRevision().get() == revisionBeforeArm);
    assert(queue.pendingTrackMask() == 0x0001);
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.telemetry(0).generation == playing.generation);
    assert(queue.telemetry(1).status ==
           SequencerTrackActivationStatus::CANCELLED);
    assert(queue.realtimeView(0).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::FROZEN);
    assert(queue.realtimeView(0).requiresLocalLoopBoundary);
    assert(queue.realtimeView(1).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::FROZEN);

    SequencerTrackActivationPlan stateAfterArm;
    assert(queue.planActivation(0x0004, 0, false, stateAfterArm));
    assert(stateAfterArm.expected.nextGeneration == playing.generation);
    assert(stateAfterArm.expected.nextOperationId ==
           playing.expected.nextOperationId);
    assert(stateAfterArm.expected.entries[0].operationId ==
           source.operationId);
    assert(stateAfterArm.expected.entries[0].target ==
           SequencerTrackActivationTarget::BEFORE);

    queue.commitHistoryTransition(transition);
    const auto publication = queue.captureRuntimePublication();
    assert(publication.queuedMask == 0x0001);
    assert(publication.cancelledMask == 0x0002);
    assert(queue.telemetryRevision().get() == revisionBeforeArm + 1U);

    std::cout
        << "[PASS] test_pure_history_plan_and_atomic_arm_cover_queue_and_cancel\n";
}

void test_planned_history_cancel_only_preserves_identifier_counters() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch source;
    assert(queue.prepare(0x0001, 0x0001, true, source));
    assert(queue.armPrepared(source));
    queue.publishPrepared(source);

    SequencerTrackActivationHistoryTransitionPlan plan;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        plan
    ));
    assert(plan.queuedMask == 0);
    assert(plan.cancelledMask == 0x0001);
    assert(plan.generation == 0);
    SequencerTrackActivationHistoryTransition transition;
    assert(queue.tryArmPlannedHistoryTransition(plan, transition));
    assert(queue.telemetry(0).status ==
           SequencerTrackActivationStatus::CANCELLED);
    assert(queue.realtimeView(0).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::FROZEN);

    SequencerTrackActivationPlan colliding;
    assert(!queue.planActivation(0x0001, 0x0001, true, colliding));

    SequencerTrackActivationPlan stateAfterArm;
    assert(queue.planActivation(0x0002, 0, false, stateAfterArm));
    assert(stateAfterArm.expected.nextGeneration ==
           plan.expected.nextGeneration);
    assert(stateAfterArm.expected.nextOperationId ==
           plan.expected.nextOperationId);

    std::cout
        << "[PASS] test_planned_history_cancel_only_preserves_identifier_counters\n";
}

void test_planned_history_arm_rejects_stale_counters_without_mutation() {
    SequencerTrackActivationQueue queue;
    const auto source = stageOperation(queue, 0x0001, 0x0001);
    SequencerTrackActivationHistoryTransitionPlan stale;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        stale
    ));

    SequencerTrackActivationBatch reservation;
    assert(queue.prepare(0x0002, 0x0002, true, reservation));
    SequencerTrackActivationHistoryTransitionPlan beforeRejectedArm;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        beforeRejectedArm
    ));
    const auto publicBefore = observePublicState(queue);
    SequencerTrackActivationHistoryTransition rejected;
    assert(!queue.tryArmPlannedHistoryTransition(stale, rejected));
    assert(!rejected.valid());
    assertPublicStateEqual(publicBefore, observePublicState(queue));
    SequencerTrackActivationHistoryTransitionPlan afterRejectedArm;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        afterRejectedArm
    ));
    assertExpectedStateEqual(
        beforeRejectedArm.expected,
        afterRejectedArm.expected
    );

    std::cout
        << "[PASS] test_planned_history_arm_rejects_stale_counters_without_mutation\n";
}

void test_planned_history_arm_rejects_stale_slots_without_mutation() {
    SequencerTrackActivationQueue queue;
    const auto source = stageOperation(queue, 0x0001, 0x0001);
    SequencerTrackActivationBatch unrelated;
    assert(queue.prepare(0x0002, 0x0002, true, unrelated));
    assert(queue.armPrepared(unrelated));
    queue.publishPrepared(unrelated);

    SequencerTrackActivationHistoryTransitionPlan stale;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        stale
    ));
    const auto publication = queue.captureRuntimePublication();
    queue.applyRuntimePublication(publication);

    SequencerTrackActivationHistoryTransitionPlan beforeRejectedArm;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        beforeRejectedArm
    ));
    const auto publicBefore = observePublicState(queue);
    SequencerTrackActivationHistoryTransition rejected;
    assert(!queue.tryArmPlannedHistoryTransition(stale, rejected));
    assert(!rejected.valid());
    assertPublicStateEqual(publicBefore, observePublicState(queue));
    SequencerTrackActivationHistoryTransitionPlan afterRejectedArm;
    assert(queue.planHistoryTransition(
        activationHistoryRef(source),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        afterRejectedArm
    ));
    assertExpectedStateEqual(
        beforeRejectedArm.expected,
        afterRejectedArm.expected
    );

    std::cout
        << "[PASS] test_planned_history_arm_rejects_stale_slots_without_mutation\n";
}

void test_planned_identifier_wrap_skips_zero_for_normal_and_history() {
    constexpr uint32_t MAX_ID = std::numeric_limits<uint32_t>::max();
    {
        SequencerTrackActivationQueue queue;
        SequencerTrackActivationQueueTestAccess::seedIdentifiers(
            queue,
            MAX_ID,
            MAX_ID
        );
        SequencerTrackActivationPlan plan;
        assert(queue.planActivation(0x0001, 0x0001, true, plan));
        assert(plan.batch.generation == 1);
        assert(plan.batch.operationId == 1);
        SequencerTrackActivationBatch armed;
        assert(queue.tryArmPlannedActivation(plan, armed));
        assert(armed.generation == 1);
        assert(armed.operationId == 1);
        SequencerTrackActivationPlan stateAfterArm;
        assert(queue.planActivation(0x0002, 0, false, stateAfterArm));
        assert(stateAfterArm.expected.nextGeneration == 1);
        assert(stateAfterArm.expected.nextOperationId == 1);
    }
    {
        SequencerTrackActivationQueue queue;
        const auto source = stageOperation(queue, 0x0001, 0x0001);
        SequencerTrackActivationQueueTestAccess::seedIdentifiers(
            queue,
            MAX_ID,
            source.operationId
        );
        SequencerTrackActivationHistoryTransitionPlan plan;
        assert(queue.planHistoryTransition(
            activationHistoryRef(source),
            SequencerTrackActivationTarget::BEFORE,
            0x0001,
            true,
            plan
        ));
        assert(plan.generation == 1);
        SequencerTrackActivationHistoryTransition armed;
        assert(queue.tryArmPlannedHistoryTransition(plan, armed));
        SequencerTrackActivationPlan stateAfterArm;
        assert(queue.planActivation(0x0002, 0, false, stateAfterArm));
        assert(stateAfterArm.expected.nextGeneration == 1);
        assert(stateAfterArm.expected.nextOperationId == source.operationId);
        assert(stateAfterArm.expected.entries[0].generation == 1);
    }

    std::cout
        << "[PASS] test_planned_identifier_wrap_skips_zero_for_normal_and_history\n";
}

void test_legacy_prepare_and_history_transition_contract_is_preserved() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch first;
    assert(queue.prepare(0x0001, 0x0001, true, first));
    assert(first.generation == 1);
    assert(first.operationId == 1);
    assert(queue.pendingTrackMask() == 0);
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::IDLE);

    SequencerTrackActivationPlan reservedState;
    assert(queue.planActivation(0x0002, 0, false, reservedState));
    assert(reservedState.expected.nextGeneration == first.generation);
    assert(reservedState.expected.nextOperationId == first.operationId);
    SequencerTrackActivationBatch second;
    assert(queue.prepare(0x0002, 0, false, second));
    assert(second.generation == first.generation + 1U);
    assert(second.operationId == first.operationId + 1U);

    assert(queue.armPrepared(first));
    queue.publishPrepared(first);
    SequencerTrackActivationHistoryTransition undo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(first),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        undo
    ));
    assert(undo.queuedMask == 0);
    assert(undo.cancelledMask == 0x0001);
    assert(queue.telemetry(0).status ==
           SequencerTrackActivationStatus::CANCELLED);
    queue.rollbackHistoryTransition(undo);
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.telemetry(0).generation == first.generation);

    std::cout
        << "[PASS] test_legacy_prepare_and_history_transition_contract_is_preserved\n";
}

void test_pending_mask_generation_and_queued_applied_telemetry() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch batch;
    assert(queue.prepare(
        0x0003,
        0x0001,
        true,
        batch,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(batch.valid());
    assert(batch.localLoopBoundaryMask == 0x0001);
    assert(batch.target == SequencerTrackActivationTarget::AFTER);
    assert(batch.origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    assert(queue.armPrepared(batch));
    queue.publishPrepared(batch);

    assert(queue.pendingTrackMask() == 0x0003);
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.telemetry(1).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.telemetry(0).origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    assert(queue.telemetry(1).origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    assert(queue.telemetryRevision().get() == 1);

    SequencerTrackActivationBatch rejected;
    assert(!queue.prepare(0x0001, 0x0003, true, rejected));

    publishRuntimeGeneration(queue);
    const auto track0 = queue.realtimeView(0);
    const auto track1 = queue.realtimeView(1);
    assert(track0.disposition == SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(track0.requiresLocalLoopBoundary);
    assert(track1.disposition == SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(!track1.requiresLocalLoopBoundary);

    assert(queue.markAppliedFromRealtime(0, batch.generation));
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::APPLIED);
    assert(queue.pendingTrackMask() == 0x0002);
    assert(queue.publishRealtimeTelemetry());
    assert(queue.telemetryRevision().get() == 2);

    std::cout << "[PASS] test_pending_mask_generation_and_queued_applied_telemetry\n";
}

void test_exclusive_solo_audibility_drives_every_activation_transition() {
    core::state::project::ProjectTrackState projectTracks;
    assert(core::state::project::setProjectTrackSoloed(
        projectTracks,
        1,
        true
    ).changed());
    const uint16_t audible = core::state::project::audibleMask(
        projectTracks,
        0x0003
    );
    assert(audible == 0x0002);

    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch paste;
    assert(queue.prepare(0x0003, audible, true, paste));
    assert(paste.localLoopBoundaryMask == 0x0002);
    assert(queue.armPrepared(paste));
    queue.publishPrepared(paste);
    publishRuntimeGeneration(queue);
    assert(!queue.realtimeView(0).requiresLocalLoopBoundary);
    assert(queue.realtimeView(1).requiresLocalLoopBoundary);
    assert(queue.markAppliedFromRealtime(0, paste.generation));
    assert(queue.markAppliedFromRealtime(1, paste.generation));
    queue.publishRealtimeTelemetry();

    SequencerTrackActivationHistoryTransition undo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::BEFORE,
        audible,
        true,
        undo
    ));
    assert(undo.queuedMask == 0x0003);
    queue.commitHistoryTransition(undo);
    publishRuntimeGeneration(queue);
    assert(!queue.realtimeView(0).requiresLocalLoopBoundary);
    assert(queue.realtimeView(1).requiresLocalLoopBoundary);

    std::cout
        << "[PASS] test_exclusive_solo_audibility_drives_every_activation_transition\n";
}

void test_partial_undo_creates_inverse_generation_and_cancels_unapplied_track() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch paste;
    assert(queue.prepare(
        0x0003,
        0x0003,
        true,
        paste,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(queue.armPrepared(paste));
    queue.publishPrepared(paste);
    publishRuntimeGeneration(queue);
    assert(queue.markAppliedFromRealtime(0, paste.generation));
    queue.publishRealtimeTelemetry();

    SequencerTrackActivationHistoryTransition undo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::BEFORE,
        0x0003,
        true,
        undo
    ));
    assert(undo.queuedMask == 0x0001);
    assert(undo.cancelledMask == 0x0002);
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.telemetry(0).generation != paste.generation);
    assert(queue.telemetry(1).status == SequencerTrackActivationStatus::CANCELLED);
    assert(queue.telemetry(0).origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    assert(queue.telemetry(1).origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    queue.commitHistoryTransition(undo);

    const auto publication = queue.captureRuntimePublication();
    assert(publication.queuedMask == 0x0001);
    assert(publication.cancelledMask == 0x0002);
    queue.applyRuntimePublication(publication);
    assert(queue.realtimeView(0).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(queue.realtimeView(1).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::NORMAL);

    const uint32_t inverseGeneration = queue.telemetry(0).generation;
    assert(queue.markAppliedFromRealtime(0, inverseGeneration));
    queue.publishRealtimeTelemetry();

    SequencerTrackActivationHistoryTransition redo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::AFTER,
        0x0003,
        true,
        redo
    ));
    assert(redo.queuedMask == 0x0003);
    assert(redo.cancelledMask == 0);
    assert(queue.telemetry(0).generation != inverseGeneration);
    assert(queue.telemetry(0).generation == queue.telemetry(1).generation);
    assert(queue.telemetry(0).origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    assert(queue.telemetry(1).origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    queue.commitHistoryTransition(redo);
    assert(queue.pendingTrackMask() == 0x0003);

    std::cout
        << "[PASS] test_partial_undo_creates_inverse_generation_and_cancels_unapplied_track\n";
}

void test_undo_before_activation_cancels_and_redo_requeues_new_generation() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch paste;
    assert(queue.prepare(0x0004, 0x0005, true, paste));
    assert(queue.armPrepared(paste));
    queue.publishPrepared(paste);

    SequencerTrackActivationHistoryTransition undo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::BEFORE,
        0x0005,
        true,
        undo
    ));
    assert(undo.queuedMask == 0);
    assert(undo.cancelledMask == 0x0004);
    queue.commitHistoryTransition(undo);
    assert(queue.pendingTrackMask() == 0);
    assert(queue.telemetry(2).status == SequencerTrackActivationStatus::CANCELLED);
    assert(queue.realtimeView(2).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::FROZEN);

    publishRuntimeGeneration(queue);
    assert(queue.realtimeView(2).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::NORMAL);

    SequencerTrackActivationHistoryTransition redo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::AFTER,
        0x0005,
        true,
        redo
    ));
    assert(redo.queuedMask == 0x0004);
    assert(queue.telemetry(2).generation != paste.generation);
    queue.commitHistoryTransition(redo);
    assert(queue.pendingTrackMask() == 0x0004);

    std::cout
        << "[PASS] test_undo_before_activation_cancels_and_redo_requeues_new_generation\n";
}

void test_failed_history_apply_rolls_activation_transition_back_exactly() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch paste;
    assert(queue.prepare(
        0x0001,
        0x0001,
        true,
        paste,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(queue.armPrepared(paste));
    queue.publishPrepared(paste);
    publishRuntimeGeneration(queue);

    SequencerTrackActivationHistoryTransition undo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        undo
    ));
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::CANCELLED);
    queue.rollbackHistoryTransition(undo);
    assert(queue.telemetry(0).status == SequencerTrackActivationStatus::QUEUED);
    assert(queue.telemetry(0).generation == paste.generation);
    assert(queue.telemetry(0).origin == SequencerTrackActivationOrigin::TRACK_PASTE);
    assert(queue.realtimeView(0).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::STAGED);

    std::cout
        << "[PASS] test_failed_history_apply_rolls_activation_transition_back_exactly\n";
}

void test_history_boundary_policy_uses_target_snapshot_masks_for_free_slot() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch paste;
    assert(queue.prepare(0x0004, 0x0001, true, paste));
    assert(paste.localLoopBoundaryMask == 0);
    assert(queue.armPrepared(paste));
    queue.publishPrepared(paste);
    publishRuntimeGeneration(queue);
    assert(queue.markAppliedFromRealtime(2, paste.generation));
    queue.publishRealtimeTelemetry();

    SequencerTrackActivationHistoryTransition undo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        undo
    ));
    assert(undo.queuedMask == 0x0004);
    queue.commitHistoryTransition(undo);
    publishRuntimeGeneration(queue);
    auto realtime = queue.realtimeView(2);
    assert(realtime.disposition ==
           SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(!realtime.requiresLocalLoopBoundary);
    const uint32_t undoGeneration = realtime.generation;
    assert(queue.markAppliedFromRealtime(2, undoGeneration));
    queue.publishRealtimeTelemetry();

    SequencerTrackActivationHistoryTransition redo;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(paste),
        SequencerTrackActivationTarget::AFTER,
        0x0005,
        true,
        redo
    ));
    assert(redo.queuedMask == 0x0004);
    queue.commitHistoryTransition(redo);
    publishRuntimeGeneration(queue);
    realtime = queue.realtimeView(2);
    assert(realtime.disposition ==
           SequencerTrackActivationRealtimeView::Disposition::STAGED);
    assert(realtime.requiresLocalLoopBoundary);

    std::cout
        << "[PASS] test_history_boundary_policy_uses_target_snapshot_masks_for_free_slot\n";
}

void test_stacked_operations_rebind_before_intermediate_boundaries() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch pasteA;
    assert(queue.prepare(0x0001, 0x0001, true, pasteA));
    assert(queue.armPrepared(pasteA));
    queue.publishPrepared(pasteA);
    applyPendingGeneration(queue, 0);

    SequencerTrackActivationBatch pasteB;
    assert(queue.prepare(0x0001, 0x0001, true, pasteB));
    assert(pasteB.operationId != pasteA.operationId);
    assert(queue.armPrepared(pasteB));
    queue.publishPrepared(pasteB);
    applyPendingGeneration(queue, 0);

    SequencerTrackActivationHistoryTransition undoB;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(pasteB),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        undoB
    ));
    assert(undoB.queuedMask == 0x0001);
    queue.commitHistoryTransition(undoB);
    const uint32_t undoBGeneration = queue.telemetry(0).generation;

    // Undo A arrives before the B->A generation reaches its boundary. The
    // single slot is rebound to A and stages the final Base target directly.
    SequencerTrackActivationHistoryTransition undoA;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(pasteA),
        SequencerTrackActivationTarget::BEFORE,
        0x0001,
        true,
        undoA
    ));
    assert(undoA.queuedMask == 0x0001);
    queue.commitHistoryTransition(undoA);
    assert(queue.telemetry(0).generation != undoBGeneration);
    applyPendingGeneration(queue, 0);

    SequencerTrackActivationHistoryTransition redoA;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(pasteA),
        SequencerTrackActivationTarget::AFTER,
        0x0001,
        true,
        redoA
    ));
    queue.commitHistoryTransition(redoA);
    const uint32_t redoAGeneration = queue.telemetry(0).generation;

    // Redo B likewise supersedes the not-yet-audible Base->A generation.
    SequencerTrackActivationHistoryTransition redoB;
    assert(queue.prepareHistoryTransition(
        activationHistoryRef(pasteB),
        SequencerTrackActivationTarget::AFTER,
        0x0001,
        true,
        redoB
    ));
    assert(redoB.queuedMask == 0x0001);
    queue.commitHistoryTransition(redoB);
    assert(queue.telemetry(0).generation != redoAGeneration);
    applyPendingGeneration(queue, 0);

    std::cout
        << "[PASS] test_stacked_operations_rebind_before_intermediate_boundaries\n";
}

void test_stacked_operations_rebind_after_each_boundary() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch pasteA;
    assert(queue.prepare(0x0001, 0x0001, true, pasteA));
    assert(queue.armPrepared(pasteA));
    queue.publishPrepared(pasteA);
    applyPendingGeneration(queue, 0);

    SequencerTrackActivationBatch pasteB;
    assert(queue.prepare(0x0001, 0x0001, true, pasteB));
    assert(queue.armPrepared(pasteB));
    queue.publishPrepared(pasteB);
    applyPendingGeneration(queue, 0);

    const auto traverse = [&](const SequencerTrackActivationBatch& batch,
                              SequencerTrackActivationTarget target) {
        SequencerTrackActivationHistoryTransition transition;
        assert(queue.prepareHistoryTransition(
            activationHistoryRef(batch),
            target,
            0x0001,
            true,
            transition
        ));
        assert(transition.queuedMask == 0x0001);
        queue.commitHistoryTransition(transition);
        applyPendingGeneration(queue, 0);
    };

    traverse(pasteB, SequencerTrackActivationTarget::BEFORE);
    traverse(pasteA, SequencerTrackActivationTarget::BEFORE);
    traverse(pasteA, SequencerTrackActivationTarget::AFTER);
    traverse(pasteB, SequencerTrackActivationTarget::AFTER);

    std::cout
        << "[PASS] test_stacked_operations_rebind_after_each_boundary\n";
}

void test_project_boundary_reset_clears_pending_and_realtime_disposition() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationBatch batch;
    assert(queue.prepare(0x0021, 0xFFFF, true, batch));
    assert(queue.armPrepared(batch));
    queue.publishPrepared(batch);
    const auto publication = queue.captureRuntimePublication();
    queue.applyRuntimePublication(publication);
    assert(queue.pendingTrackMask() == 0x0021);
    assert(queue.realtimeView(0).disposition ==
           SequencerTrackActivationRealtimeView::Disposition::STAGED);

    queue.reset();
    assert(queue.pendingTrackMask() == 0);
    for (uint8_t track = 0; track < SequencerTrackActivationQueue::TRACK_COUNT;
         ++track) {
        assert(queue.telemetry(track).status ==
               SequencerTrackActivationStatus::IDLE);
        assert(queue.realtimeView(track).disposition ==
               SequencerTrackActivationRealtimeView::Disposition::NORMAL);
    }

    std::cout
        << "[PASS] test_project_boundary_reset_clears_pending_and_realtime_disposition\n";
}

void test_direct_mutation_guard_capture_is_validated_and_pure() {
    SequencerTrackActivationQueue queue;
    SequencerTrackActivationMutationGuard invalid;
    invalid.protectedTrackMask = 0xFFFFU;
    assert(!queue.captureMutationGuard(0U, invalid));
    assert(!invalid.valid());

    const auto before = observePublicState(queue);
    SequencerTrackActivationMutationGuard guard;
    assert(queue.captureMutationGuard(0x0003U, guard));
    assert(guard.valid());
    assert(guard.protectedTrackMask == 0x0003U);
    assert(queue.mutationGuardMatches(guard));
    assertPublicStateEqual(before, observePublicState(queue));

    // Planning is deliberately read-only and therefore cannot stale a guard.
    SequencerTrackActivationPlan plan;
    assert(queue.planActivation(
        0x000CU,
        0x0004U,
        true,
        plan,
        SequencerTrackActivationOrigin::TRACK_PASTE
    ));
    assert(queue.mutationGuardMatches(guard));
    assertPublicStateEqual(before, observePublicState(queue));

    std::cout
        << "[PASS] test_direct_mutation_guard_capture_is_validated_and_pure\n";
}

void test_direct_mutation_guard_rejects_only_pending_protected_tracks() {
    for (const bool transportPlaying : {false, true}) {
        SequencerTrackActivationQueue queue;
        SequencerTrackActivationBatch pending;
        assert(queue.prepare(
            0x0001U,
            0x0001U,
            transportPlaying,
            pending,
            SequencerTrackActivationOrigin::TRACK_PASTE
        ));
        assert(queue.armPrepared(pending));

        SequencerTrackActivationMutationGuard protectedCollision;
        assert(!queue.captureMutationGuard(
            0x0001U,
            protectedCollision
        ));
        assert(!protectedCollision.valid());

        SequencerTrackActivationMutationGuard unrelated;
        assert(queue.captureMutationGuard(0x0002U, unrelated));
        assert(unrelated.valid());
        assert(queue.mutationGuardMatches(unrelated));
    }

    std::cout
        << "[PASS] test_direct_mutation_guard_rejects_only_pending_protected_tracks\n";
}

void test_direct_mutation_guard_detects_representative_queue_changes() {
    {
        SequencerTrackActivationQueue queue;
        SequencerTrackActivationMutationGuard guard;
        assert(queue.captureMutationGuard(0x0001U, guard));

        // Legacy prepare changes only the queue identifier counters.
        SequencerTrackActivationBatch reservation;
        assert(queue.prepare(0x0002U, 0U, false, reservation));
        assert(!queue.mutationGuardMatches(guard));
    }

    {
        SequencerTrackActivationQueue queue;
        SequencerTrackActivationBatch batch;
        assert(queue.prepare(0x0002U, 0x0002U, true, batch));
        assert(queue.armPrepared(batch));

        SequencerTrackActivationMutationGuard guard;
        assert(queue.captureMutationGuard(0x0001U, guard));
        assert(queue.mutationGuardMatches(guard));

        // Publication changes both the slot phase and telemetry revision.
        queue.publishPrepared(batch);
        assert(!queue.mutationGuardMatches(guard));
    }

    {
        SequencerTrackActivationQueue queue;
        SequencerTrackActivationMutationGuard guard;
        assert(queue.captureMutationGuard(0x0001U, guard));

        SequencerTrackActivationPlan plan;
        assert(queue.planActivation(0x0002U, 0U, false, plan));
        SequencerTrackActivationBatch armed;
        assert(queue.tryArmPlannedActivation(plan, armed));
        assert(!queue.mutationGuardMatches(guard));
    }

    std::cout
        << "[PASS] test_direct_mutation_guard_detects_representative_queue_changes\n";
}

}  // namespace

int main() {
    test_pure_normal_plans_preserve_state_and_boundary_masks();
    test_planned_normal_arm_is_atomic_and_advances_once();
    test_planned_normal_arm_rejects_stale_counters_without_mutation();
    test_planned_normal_arm_rejects_stale_slots_without_mutation();
    test_pure_history_plan_and_atomic_arm_cover_queue_and_cancel();
    test_planned_history_cancel_only_preserves_identifier_counters();
    test_planned_history_arm_rejects_stale_counters_without_mutation();
    test_planned_history_arm_rejects_stale_slots_without_mutation();
    test_planned_identifier_wrap_skips_zero_for_normal_and_history();
    test_legacy_prepare_and_history_transition_contract_is_preserved();
    test_pending_mask_generation_and_queued_applied_telemetry();
    test_exclusive_solo_audibility_drives_every_activation_transition();
    test_partial_undo_creates_inverse_generation_and_cancels_unapplied_track();
    test_undo_before_activation_cancels_and_redo_requeues_new_generation();
    test_failed_history_apply_rolls_activation_transition_back_exactly();
    test_history_boundary_policy_uses_target_snapshot_masks_for_free_slot();
    test_stacked_operations_rebind_before_intermediate_boundaries();
    test_stacked_operations_rebind_after_each_boundary();
    test_project_boundary_reset_clears_pending_and_realtime_disposition();
    test_direct_mutation_guard_capture_is_validated_and_pure();
    test_direct_mutation_guard_rejects_only_pending_protected_tracks();
    test_direct_mutation_guard_detects_representative_queue_changes();
    std::cout << "All SequencerTrackActivationQueue tests passed\n";
    return 0;
}
