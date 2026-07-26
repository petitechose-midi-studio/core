#include "state/sequencer/SequencerTrackActivationQueue.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/realtime/InterruptGuard.hpp>

namespace core::state::sequencer {

FLASHMEM void SequencerTrackActivationQueue::reset() {
    {
        oc::realtime::InterruptGuard lock;
        for (auto& entry : entries_) {
            entry.phase = InternalPhase::IDLE;
            entry.requiresLocalLoopBoundary = 0;
            entry.target = SequencerTrackActivationTarget::AFTER;
            entry.origin = SequencerTrackActivationOrigin::UNSPECIFIED;
            entry.generation = 0;
            entry.operationId = 0;
        }
        next_generation_ = 0;
        next_operation_id_ = 0;
    }
    bumpTelemetryRevision_();
}

bool SequencerTrackActivationQueue::isPending_(InternalPhase phase) {
    return phase == InternalPhase::ARMED ||
           phase == InternalPhase::QUEUED ||
           phase == InternalPhase::STAGED;
}

bool SequencerTrackActivationQueue::isFrozen_(InternalPhase phase) {
    return phase == InternalPhase::ARMED ||
           phase == InternalPhase::QUEUED ||
           phase == InternalPhase::CANCELLED_FROZEN;
}

bool SequencerTrackActivationQueue::runtimeIsTarget_(InternalPhase phase) {
    return phase == InternalPhase::APPLIED_PENDING_TELEMETRY ||
           phase == InternalPhase::APPLIED;
}

SequencerTrackActivationStatus SequencerTrackActivationQueue::telemetryStatus_(
    InternalPhase phase
) {
    switch (phase) {
        case InternalPhase::ARMED:
        case InternalPhase::QUEUED:
        case InternalPhase::STAGED:
            return SequencerTrackActivationStatus::QUEUED;
        case InternalPhase::APPLIED_PENDING_TELEMETRY:
        case InternalPhase::APPLIED:
            return SequencerTrackActivationStatus::APPLIED;
        case InternalPhase::CANCELLED_FROZEN:
        case InternalPhase::CANCELLED:
            return SequencerTrackActivationStatus::CANCELLED;
        default:
            return SequencerTrackActivationStatus::IDLE;
    }
}

uint16_t SequencerTrackActivationQueue::sanitizeMask_(uint16_t trackMask) {
    return static_cast<uint16_t>(trackMask & ALL_TRACKS_MASK);
}

FLASHMEM void SequencerTrackActivationQueue::bumpTelemetryRevision_() {
    telemetry_revision_.set(telemetry_revision_.get() + 1U);
}

FLASHMEM bool SequencerTrackActivationQueue::prepare(
    uint16_t trackMask,
    uint16_t targetAudibleMask,
    bool transportPlaying,
    SequencerTrackActivationBatch& out,
    SequencerTrackActivationOrigin origin
) {
    out = {};
    const uint16_t sanitized = sanitizeMask_(trackMask);
    if (sanitized == 0) return false;

    oc::realtime::InterruptGuard lock;
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((sanitized & bit) == 0) continue;
        if (isPending_(entries_[track].phase)) return false;
    }

    ++next_generation_;
    if (next_generation_ == 0) ++next_generation_;
    ++next_operation_id_;
    if (next_operation_id_ == 0) ++next_operation_id_;
    const uint16_t audibleMask = sanitizeMask_(targetAudibleMask);
    out.trackMask = sanitized;
    out.localLoopBoundaryMask = transportPlaying
        ? static_cast<uint16_t>(sanitized & audibleMask)
        : 0;
    out.generation = next_generation_;
    out.operationId = next_operation_id_;
    out.target = SequencerTrackActivationTarget::AFTER;
    out.origin = origin;
    return true;
}

FLASHMEM bool SequencerTrackActivationQueue::armPrepared(
    const SequencerTrackActivationBatch& batch
) {
    const uint16_t mask = sanitizeMask_(batch.trackMask);
    if (!batch.valid() || mask != batch.trackMask) return false;

    oc::realtime::InterruptGuard lock;
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((mask & bit) == 0) continue;
        if (isPending_(entries_[track].phase)) return false;
    }
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((mask & bit) == 0) continue;
        entries_[track].generation = batch.generation;
        entries_[track].operationId = batch.operationId;
        entries_[track].target = batch.target;
        entries_[track].origin = batch.origin;
        entries_[track].requiresLocalLoopBoundary =
            (batch.localLoopBoundaryMask & bit) != 0 ? 1U : 0U;
        entries_[track].phase = InternalPhase::ARMED;
    }
    return true;
}

FLASHMEM void SequencerTrackActivationQueue::publishPrepared(
    const SequencerTrackActivationBatch& batch
) {
    bool changed = false;
    {
        oc::realtime::InterruptGuard lock;
        const uint16_t mask = sanitizeMask_(batch.trackMask);
        for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
            const uint16_t bit = static_cast<uint16_t>(1U << track);
            if ((mask & bit) == 0) continue;
            auto& entry = entries_[track];
            if (entry.phase != InternalPhase::ARMED ||
                entry.generation != batch.generation) {
                continue;
            }
            entry.phase = InternalPhase::QUEUED;
            changed = true;
        }
    }
    if (changed) bumpTelemetryRevision_();
}

FLASHMEM uint16_t SequencerTrackActivationQueue::pendingTrackMask() const {
    uint16_t mask = 0;
    oc::realtime::InterruptGuard lock;
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        if (isPending_(entries_[track].phase)) {
            mask = static_cast<uint16_t>(mask | static_cast<uint16_t>(1U << track));
        }
    }
    return mask;
}

FLASHMEM SequencerTrackActivationTelemetry SequencerTrackActivationQueue::telemetry(
    uint8_t trackIndex
) const {
    if (trackIndex >= TRACK_COUNT) return {};
    oc::realtime::InterruptGuard lock;
    const auto& entry = entries_[trackIndex];
    return {telemetryStatus_(entry.phase), entry.generation, entry.origin};
}

FLASHMEM SequencerTrackActivationRuntimePublication
SequencerTrackActivationQueue::captureRuntimePublication() const {
    SequencerTrackActivationRuntimePublication publication;
    oc::realtime::InterruptGuard lock;
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const auto& entry = entries_[track];
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if (entry.phase == InternalPhase::QUEUED) {
            publication.queuedMask = static_cast<uint16_t>(publication.queuedMask | bit);
        } else if (entry.phase == InternalPhase::CANCELLED_FROZEN) {
            publication.cancelledMask = static_cast<uint16_t>(
                publication.cancelledMask | bit
            );
        } else {
            continue;
        }
        publication.generations[track] = entry.generation;
    }
    return publication;
}

void SequencerTrackActivationQueue::applyRuntimePublication(
    const SequencerTrackActivationRuntimePublication& publication
) {
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        auto& entry = entries_[track];
        if ((publication.queuedMask & bit) != 0 &&
            entry.phase == InternalPhase::QUEUED &&
            entry.generation == publication.generations[track]) {
            entry.phase = InternalPhase::STAGED;
        } else if ((publication.cancelledMask & bit) != 0 &&
                   entry.phase == InternalPhase::CANCELLED_FROZEN &&
                   entry.generation == publication.generations[track]) {
            entry.phase = InternalPhase::CANCELLED;
        }
    }
}

SequencerTrackActivationRealtimeView SequencerTrackActivationQueue::realtimeView(
    uint8_t trackIndex
) const {
    if (trackIndex >= TRACK_COUNT) return {};
    const auto& entry = entries_[trackIndex];
    if (entry.phase == InternalPhase::STAGED) {
        return {
            SequencerTrackActivationRealtimeView::Disposition::STAGED,
            entry.generation,
            entry.requiresLocalLoopBoundary != 0,
        };
    }
    if (isFrozen_(entry.phase)) {
        return {
            SequencerTrackActivationRealtimeView::Disposition::FROZEN,
            entry.generation,
            entry.requiresLocalLoopBoundary != 0,
        };
    }
    return {};
}

bool SequencerTrackActivationQueue::markAppliedFromRealtime(
    uint8_t trackIndex,
    uint32_t generation
) {
    if (trackIndex >= TRACK_COUNT || generation == 0) return false;
    auto& entry = entries_[trackIndex];
    if (entry.phase != InternalPhase::STAGED || entry.generation != generation) {
        return false;
    }
    entry.phase = InternalPhase::APPLIED_PENDING_TELEMETRY;
    return true;
}

FLASHMEM bool SequencerTrackActivationQueue::publishRealtimeTelemetry() {
    bool changed = false;
    {
        oc::realtime::InterruptGuard lock;
        for (auto& entry : entries_) {
            if (entry.phase != InternalPhase::APPLIED_PENDING_TELEMETRY) continue;
            entry.phase = InternalPhase::APPLIED;
            changed = true;
        }
    }
    if (changed) bumpTelemetryRevision_();
    return changed;
}

FLASHMEM bool SequencerTrackActivationQueue::prepareHistoryTransition(
    const SequencerTrackActivationHistoryRef& reference,
    SequencerTrackActivationTarget desiredTarget,
    uint16_t targetAudibleMask,
    bool transportPlaying,
    SequencerTrackActivationHistoryTransition& out
) {
    out = {};
    if (!reference.valid()) return false;
    const uint16_t mask = sanitizeMask_(reference.trackMask);
    if (mask != reference.trackMask) return false;

    oc::realtime::InterruptGuard lock;
    uint16_t queuedMask = 0;
    uint16_t cancelledMask = 0;
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((mask & bit) == 0) continue;
        const auto& entry = entries_[track];
        out.previous[track] = {
            static_cast<uint8_t>(entry.phase),
            entry.requiresLocalLoopBoundary,
            entry.target,
            entry.origin,
            entry.generation,
            entry.operationId,
        };

        // A newer history operation may have reused this Track's single
        // realtime slot. Sequential Undo/Redo is still safe: the history
        // snapshot being restored is the complete audible target, so rebind
        // the slot and stage it directly instead of requiring an obsolete
        // operation id to remain resident forever.
        const bool sameOperation =
            entry.phase != InternalPhase::IDLE &&
            entry.operationId == reference.operationId;
        if (!sameOperation) {
            queuedMask = static_cast<uint16_t>(queuedMask | bit);
            continue;
        }

        const bool runtimeMatchesEntryTarget = runtimeIsTarget_(entry.phase);
        const auto runtimeTarget = runtimeMatchesEntryTarget
            ? entry.target
            : (entry.target == SequencerTrackActivationTarget::AFTER
                ? SequencerTrackActivationTarget::BEFORE
                : SequencerTrackActivationTarget::AFTER);
        if (runtimeTarget == desiredTarget) {
            if (isPending_(entry.phase)) {
                cancelledMask = static_cast<uint16_t>(cancelledMask | bit);
            }
        } else {
            queuedMask = static_cast<uint16_t>(queuedMask | bit);
        }
    }

    const uint16_t audibleMask = sanitizeMask_(targetAudibleMask);
    uint32_t transitionGeneration = 0;
    if (queuedMask != 0) {
        ++next_generation_;
        if (next_generation_ == 0) ++next_generation_;
        transitionGeneration = next_generation_;
    }
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((mask & bit) == 0) continue;
        auto& entry = entries_[track];
        if ((cancelledMask & bit) != 0) {
            entry.phase = InternalPhase::CANCELLED_FROZEN;
        } else if ((queuedMask & bit) != 0) {
            entry.generation = transitionGeneration;
            entry.operationId = reference.operationId;
            entry.target = desiredTarget;
            entry.origin = reference.origin;
            entry.requiresLocalLoopBoundary = transportPlaying &&
                (audibleMask & bit) != 0 ? 1U : 0U;
            entry.phase = InternalPhase::ARMED;
        }
    }
    out.reference = reference;
    out.desiredTarget = desiredTarget;
    out.touchedMask = mask;
    out.queuedMask = queuedMask;
    out.cancelledMask = cancelledMask;
    return true;
}

FLASHMEM void SequencerTrackActivationQueue::commitHistoryTransition(
    const SequencerTrackActivationHistoryTransition& transition
) {
    if (!transition.valid()) return;
    bool changed = transition.cancelledMask != 0;
    {
        oc::realtime::InterruptGuard lock;
        for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
            const uint16_t bit = static_cast<uint16_t>(1U << track);
            if ((transition.queuedMask & bit) == 0) continue;
            auto& entry = entries_[track];
            if (entry.phase != InternalPhase::ARMED ||
                entry.operationId != transition.reference.operationId ||
                entry.target != transition.desiredTarget) {
                continue;
            }
            entry.phase = InternalPhase::QUEUED;
            changed = true;
        }
    }
    if (changed) bumpTelemetryRevision_();
}

FLASHMEM void SequencerTrackActivationQueue::rollbackHistoryTransition(
    const SequencerTrackActivationHistoryTransition& transition
) {
    if (!transition.valid()) return;
    {
        oc::realtime::InterruptGuard lock;
        for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
            const uint16_t bit = static_cast<uint16_t>(1U << track);
            if ((transition.touchedMask & bit) == 0) continue;
            const auto& previous = transition.previous[track];
            auto& entry = entries_[track];
            entry.phase = static_cast<InternalPhase>(previous.phase);
            entry.requiresLocalLoopBoundary = previous.requiresLocalLoopBoundary;
            entry.target = previous.target;
            entry.origin = previous.origin;
            entry.generation = previous.generation;
            entry.operationId = previous.operationId;
        }
    }
    bumpTelemetryRevision_();
}

}  // namespace core::state::sequencer
