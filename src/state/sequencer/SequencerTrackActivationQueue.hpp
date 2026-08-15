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

static_assert(
    static_cast<uint8_t>(SequencerTrackActivationOrigin::HISTORY) <= 0x03U,
    "activation guard packs origin in two bits"
);
static_assert(
    static_cast<uint8_t>(SequencerTrackActivationTarget::AFTER) <= 0x01U,
    "activation guard packs target in one bit"
);

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

struct SequencerTrackActivationEntrySnapshot {
    uint8_t phase = 0;
    uint8_t requiresLocalLoopBoundary = 0;
    SequencerTrackActivationTarget target = SequencerTrackActivationTarget::AFTER;
    SequencerTrackActivationOrigin origin =
        SequencerTrackActivationOrigin::UNSPECIFIED;
    uint32_t generation = 0;
    uint32_t operationId = 0;
};

/** Exact queue state captured by a pure activation plan. */
struct SequencerTrackActivationExpectedState {
    std::array<
        SequencerTrackActivationEntrySnapshot,
        SequencerTrackBankState::TRACK_COUNT
    > entries{};
    uint32_t nextGeneration = 0;
    uint32_t nextOperationId = 0;
    uint32_t telemetryRevision = 0;
};

/**
 * Pure normal-activation plan. Building this object reserves no identifier and
 * changes no queue state. The identifiers become official only when the plan
 * passes the atomic arm gate.
 */
struct SequencerTrackActivationPlan {
    SequencerTrackActivationBatch batch{};
    SequencerTrackActivationExpectedState expected{};

    bool valid() const { return batch.valid(); }
};
static_assert(
    sizeof(SequencerTrackActivationPlan) <= 256,
    "normal activation plan must remain within the planner frame target"
);

/**
 * Exact, non-mutating guard for a direct Track Structure action.
 *
 * Direct create/remove/Macro topology commands never reserve an activation
 * slot. They capture the complete queue state and reject when another pending
 * activation intersects their affected/old/new Track mask. Final validation
 * repeats both checks without cancelling or replacing the other transaction.
 */
struct SequencerTrackActivationMutationGuard {
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT> generations{};
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT> operationIds{};
    uint32_t nextGeneration = 0U;
    uint32_t nextOperationId = 0U;
    uint32_t telemetryRevision = 0U;
    // phase[0..2], local-boundary[3], target[4], origin[5..6].
    std::array<uint8_t, SequencerTrackBankState::TRACK_COUNT> packedEntries{};
    uint16_t protectedTrackMask = 0U;

    [[nodiscard]] bool valid() const { return protectedTrackMask != 0U; }
};
static_assert(
    sizeof(SequencerTrackActivationMutationGuard) <= 160U,
    "direct activation guard must remain within the planner frame target"
);

struct SequencerTrackActivationHistoryRef {
    uint16_t trackMask = 0;
    uint32_t operationId = 0;
    SequencerTrackActivationOrigin origin =
        SequencerTrackActivationOrigin::UNSPECIFIED;

    bool valid() const { return trackMask != 0 && operationId != 0; }
};

struct SequencerTrackActivationHistoryPlan {
    SequencerTrackActivationHistoryRef reference{};
    // Canonical Project Track audibility at the history target. This already
    // includes structural enablement, Mute and the exclusive Solo selection.
    uint16_t targetAudibleMask = 0;

    bool valid() const { return reference.valid(); }
};

struct SequencerTrackActivationHistoryTransition {
    using EntrySnapshot = SequencerTrackActivationEntrySnapshot;

    SequencerTrackActivationHistoryRef reference{};
    SequencerTrackActivationTarget desiredTarget =
        SequencerTrackActivationTarget::BEFORE;
    uint16_t touchedMask = 0;
    uint16_t queuedMask = 0;
    uint16_t cancelledMask = 0;
    std::array<EntrySnapshot, SequencerTrackBankState::TRACK_COUNT> previous{};

    bool valid() const { return reference.valid() && touchedMask != 0; }
};

/**
 * Pure Undo/Redo activation plan. It retains the exact queue state against
 * which queued/cancelled masks were derived, but does not freeze any slot.
 */
struct SequencerTrackActivationHistoryTransitionPlan {
    SequencerTrackActivationHistoryRef reference{};
    SequencerTrackActivationTarget desiredTarget =
        SequencerTrackActivationTarget::BEFORE;
    uint16_t touchedMask = 0;
    uint16_t queuedMask = 0;
    uint16_t cancelledMask = 0;
    uint16_t localLoopBoundaryMask = 0;
    uint32_t generation = 0;
    SequencerTrackActivationExpectedState expected{};

    bool valid() const { return reference.valid() && touchedMask != 0; }
};
static_assert(
    sizeof(SequencerTrackActivationHistoryTransitionPlan) <= 256,
    "History activation plan must remain within the planner frame target"
);

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

    /**
     * Plans activation against canonical Project Track audibility. The caller
     * resolves structure, Mute and exclusive Solo before crossing this fixed
     * realtime hand-off; the queue never consults secondary state mirrors.
     */
    bool prepare(uint16_t trackMask,
                 uint16_t targetAudibleMask,
                 bool transportPlaying,
                 SequencerTrackActivationBatch& out,
                 SequencerTrackActivationOrigin origin =
                     SequencerTrackActivationOrigin::UNSPECIFIED);
    bool armPrepared(const SequencerTrackActivationBatch& batch);
    void publishPrepared(const SequencerTrackActivationBatch& batch);

    /**
     * Builds a scalar activation plan without reserving counters or touching
     * queue slots. Arbitrary fallible work may occur before the atomic gate.
     */
    bool planActivation(
        uint16_t trackMask,
        uint16_t targetAudibleMask,
        bool transportPlaying,
        SequencerTrackActivationPlan& out,
        SequencerTrackActivationOrigin origin =
            SequencerTrackActivationOrigin::UNSPECIFIED
    ) const;
    bool tryArmPlannedActivation(
        const SequencerTrackActivationPlan& plan,
        SequencerTrackActivationBatch& out
    );

    /** Captures an exact direct-mutation guard without changing queue state. */
    [[nodiscard]] bool captureMutationGuard(
        uint16_t protectedTrackMask,
        SequencerTrackActivationMutationGuard& out
    ) const;
    /** Exact final match plus repeated pending-intersection rejection. */
    [[nodiscard]] bool mutationGuardMatches(
        const SequencerTrackActivationMutationGuard& guard
    ) const;

    uint16_t pendingTrackMask() const;
    SequencerTrackActivationTelemetry telemetry(uint8_t trackIndex) const;
    oc::state::Signal<uint32_t, 4>& telemetryRevision() { return telemetry_revision_; }
    const oc::state::Signal<uint32_t, 4>& telemetryRevision() const {
        return telemetry_revision_;
    }

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

    /** Replans Undo/Redo against the canonical audible mask of its target. */
    bool planHistoryTransition(
        const SequencerTrackActivationHistoryRef& reference,
        SequencerTrackActivationTarget desiredTarget,
        uint16_t targetAudibleMask,
        bool transportPlaying,
        SequencerTrackActivationHistoryTransitionPlan& out
    ) const;
    bool tryArmPlannedHistoryTransition(
        const SequencerTrackActivationHistoryTransitionPlan& plan,
        SequencerTrackActivationHistoryTransition& out
    );

    /** Atomic plan-and-arm convenience used by History traversal. */
    bool prepareHistoryTransition(
        const SequencerTrackActivationHistoryRef& reference,
        SequencerTrackActivationTarget desiredTarget,
        uint16_t targetAudibleMask,
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
    static_assert(
        static_cast<uint8_t>(InternalPhase::CANCELLED) <= 0x07U,
        "activation guard packs phase in three bits"
    );

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
    static uint32_t nextNonZeroIdentifier_(uint32_t current);
    static bool sameEntry_(
        const Entry& entry,
        const SequencerTrackActivationEntrySnapshot& expected
    );
    static bool packMutationGuardEntry_(
        const Entry& entry,
        uint8_t& out
    );
    void captureExpectedStateLocked_(
        SequencerTrackActivationExpectedState& out
    ) const;
    bool expectedStateMatchesLocked_(
        const SequencerTrackActivationExpectedState& expected
    ) const;
    bool buildHistoryTransitionPlanLocked_(
        const SequencerTrackActivationHistoryRef& reference,
        SequencerTrackActivationTarget desiredTarget,
        uint16_t targetAudibleMask,
        bool transportPlaying,
        SequencerTrackActivationHistoryTransitionPlan& out
    ) const;
    bool tryArmPlannedHistoryTransitionLocked_(
        const SequencerTrackActivationHistoryTransitionPlan& plan,
        SequencerTrackActivationHistoryTransition& out
    );
    void bumpTelemetryRevision_();

    friend struct SequencerTrackActivationQueueTestAccess;

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
