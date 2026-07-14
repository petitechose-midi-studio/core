#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

enum class SequencerTrackActivationStatus : uint8_t {
    IDLE = 0,
    QUEUED,
    APPLIED,
    CANCELLED,
};

enum class SequencerTrackActivationOrigin : uint8_t {
    UNSPECIFIED = 0,
    TRACK_PASTE,
    STEP_PRESET,
    HISTORY,
};

enum class SequencerTrackActivationTarget : uint8_t {
    BEFORE = 0,
    AFTER,
};

struct SequencerTrackActivationBatch {
    uint16_t trackMask = 0;
    uint16_t localLoopBoundaryMask = 0;
    uint32_t generation = 0;
    uint32_t operationId = 0;
    SequencerTrackActivationTarget target = SequencerTrackActivationTarget::AFTER;
    SequencerTrackActivationOrigin origin =
        SequencerTrackActivationOrigin::UNSPECIFIED;

    bool valid() const {
        return trackMask != 0 && generation != 0 && operationId != 0;
    }
};

struct SequencerTrackActivationHistoryRef {
    uint16_t trackMask = 0;
    uint32_t operationId = 0;
    SequencerTrackActivationOrigin origin =
        SequencerTrackActivationOrigin::UNSPECIFIED;

    bool valid() const { return trackMask != 0 && operationId != 0; }
};

struct SequencerTrackActivationHistoryPlan {
    SequencerTrackActivationHistoryRef reference{};
    uint16_t targetEnabledMask = 0;
    uint16_t targetMutedMask = 0;

    bool valid() const { return reference.valid(); }
};

struct SequencerTrackActivationHistoryTransition {
    struct EntrySnapshot {
        uint8_t phase = 0;
        uint8_t requiresLocalLoopBoundary = 0;
        SequencerTrackActivationTarget target = SequencerTrackActivationTarget::AFTER;
        SequencerTrackActivationOrigin origin =
            SequencerTrackActivationOrigin::UNSPECIFIED;
        uint32_t generation = 0;
        uint32_t operationId = 0;
    };

    SequencerTrackActivationHistoryRef reference{};
    SequencerTrackActivationTarget desiredTarget =
        SequencerTrackActivationTarget::BEFORE;
    uint16_t touchedMask = 0;
    uint16_t queuedMask = 0;
    uint16_t cancelledMask = 0;
    std::array<EntrySnapshot, SequencerTrackBankState::TRACK_COUNT> previous{};

    bool valid() const { return reference.valid() && touchedMask != 0; }
};

struct SequencerTrackActivationRuntimePublication {
    uint16_t queuedMask = 0;
    uint16_t cancelledMask = 0;
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT> generations{};

    bool empty() const { return queuedMask == 0 && cancelledMask == 0; }
};

struct SequencerTrackActivationRealtimeView {
    enum class Disposition : uint8_t {
        NORMAL = 0,
        FROZEN,
        STAGED,
    };

    Disposition disposition = Disposition::NORMAL;
    uint32_t generation = 0;
    bool requiresLocalLoopBoundary = false;
};

struct SequencerTrackActivationTelemetry {
    SequencerTrackActivationStatus status = SequencerTrackActivationStatus::IDLE;
    uint32_t generation = 0;
    SequencerTrackActivationOrigin origin =
        SequencerTrackActivationOrigin::UNSPECIFIED;
};

/**
 * Fixed-capacity Track activation hand-off shared by editor/history and the
 * realtime playback lane. There is exactly one slot per Track and no method
 * reachable from the realtime lane allocates or publishes Signals.
 */
class SequencerTrackActivationQueue {
public:
    static constexpr uint8_t TRACK_COUNT = SequencerTrackBankState::TRACK_COUNT;

    /** Clears every editor/runtime hand-off at a Project generation boundary. */
    void reset();

    bool prepare(uint16_t trackMask,
                 uint16_t enabledMask,
                 uint16_t mutedMask,
                 bool transportPlaying,
                 SequencerTrackActivationBatch& out,
                 SequencerTrackActivationOrigin origin =
                     SequencerTrackActivationOrigin::UNSPECIFIED);
    bool armPrepared(const SequencerTrackActivationBatch& batch);
    void publishPrepared(const SequencerTrackActivationBatch& batch);

    uint16_t pendingTrackMask() const;
    SequencerTrackActivationTelemetry telemetry(uint8_t trackIndex) const;
    oc::state::Signal<uint32_t, 4>& telemetryRevision() { return telemetry_revision_; }

    SequencerTrackActivationRuntimePublication captureRuntimePublication() const;

    // Called only by SequencerRuntimeGraphBank's companion publisher while its
    // InterruptGuard is already held. It publishes flat + graph + activation
    // disposition as one indivisible runtime generation.
    void applyRuntimePublication(
        const SequencerTrackActivationRuntimePublication& publication
    );

    // Realtime lane API: fixed-size reads/writes only, with no Signal traffic.
    SequencerTrackActivationRealtimeView realtimeView(uint8_t trackIndex) const;
    bool markAppliedFromRealtime(uint8_t trackIndex, uint32_t generation);

    // Main-loop telemetry bridge for APPLIED transitions produced by the ISR.
    bool publishRealtimeTelemetry();

    bool prepareHistoryTransition(
        const SequencerTrackActivationHistoryRef& reference,
        SequencerTrackActivationTarget desiredTarget,
        uint16_t enabledMask,
        uint16_t mutedMask,
        bool transportPlaying,
        SequencerTrackActivationHistoryTransition& out
    );
    void commitHistoryTransition(
        const SequencerTrackActivationHistoryTransition& transition
    );
    void rollbackHistoryTransition(
        const SequencerTrackActivationHistoryTransition& transition
    );

private:
    enum class InternalPhase : uint8_t {
        IDLE = 0,
        ARMED,
        QUEUED,
        STAGED,
        APPLIED_PENDING_TELEMETRY,
        APPLIED,
        CANCELLED_FROZEN,
        CANCELLED,
    };

    struct Entry {
        volatile InternalPhase phase = InternalPhase::IDLE;
        volatile uint8_t requiresLocalLoopBoundary = 0;
        volatile SequencerTrackActivationTarget target =
            SequencerTrackActivationTarget::AFTER;
        volatile SequencerTrackActivationOrigin origin =
            SequencerTrackActivationOrigin::UNSPECIFIED;
        volatile uint32_t generation = 0;
        volatile uint32_t operationId = 0;
    };

    static constexpr uint16_t ALL_TRACKS_MASK =
        static_cast<uint16_t>((1U << TRACK_COUNT) - 1U);

    static bool isPending_(InternalPhase phase);
    static bool isFrozen_(InternalPhase phase);
    static bool runtimeIsTarget_(InternalPhase phase);
    static SequencerTrackActivationStatus telemetryStatus_(InternalPhase phase);
    static uint16_t sanitizeMask_(uint16_t trackMask);
    void bumpTelemetryRevision_();

    std::array<Entry, TRACK_COUNT> entries_{};
    uint32_t next_generation_ = 0;
    uint32_t next_operation_id_ = 0;
    oc::state::Signal<uint32_t, 4> telemetry_revision_{0};
};

inline SequencerTrackActivationHistoryRef activationHistoryRef(
    const SequencerTrackActivationBatch& batch
) {
    return {batch.trackMask, batch.operationId, batch.origin};
}

}  // namespace core::state::sequencer
