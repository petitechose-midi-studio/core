#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"

namespace {

using core::state::sequencer::SequencerTrackActivationBatch;
using core::state::sequencer::SequencerTrackActivationHistoryTransition;
using core::state::sequencer::SequencerTrackActivationOrigin;
using core::state::sequencer::SequencerTrackActivationQueue;
using core::state::sequencer::SequencerTrackActivationRealtimeView;
using core::state::sequencer::SequencerTrackActivationStatus;
using core::state::sequencer::SequencerTrackActivationTarget;
using core::state::sequencer::activationHistoryRef;

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

}  // namespace

int main() {
    test_pending_mask_generation_and_queued_applied_telemetry();
    test_exclusive_solo_audibility_drives_every_activation_transition();
    test_partial_undo_creates_inverse_generation_and_cancels_unapplied_track();
    test_undo_before_activation_cancels_and_redo_requeues_new_generation();
    test_failed_history_apply_rolls_activation_transition_back_exactly();
    test_history_boundary_policy_uses_target_snapshot_masks_for_free_slot();
    test_stacked_operations_rebind_before_intermediate_boundaries();
    test_stacked_operations_rebind_after_each_boundary();
    test_project_boundary_reset_clears_pending_and_realtime_disposition();
    std::cout << "All SequencerTrackActivationQueue tests passed\n";
    return 0;
}
