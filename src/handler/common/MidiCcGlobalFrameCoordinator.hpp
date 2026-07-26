#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerCcLaneRuntime.hpp"
#include "sequencer/TemporalMidiCcAuthorSpool.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::sequencer {
struct ProjectTrackRuntimeSnapshot;
}

namespace core::handler {

/** Main/ISR-published complete non-lane author frame. */
struct MidiCcPersistentAuthorFrame {
    static constexpr uint16_t MAX_CANDIDATES = 256;

    uint32_t revision = 0;
    uint16_t candidateCount = 0;
    std::array<
        core::state::shared::MidiCcCandidate,
        MAX_CANDIDATES
    > candidates{};
};

enum class MidiCcGlobalFrameStatus : uint8_t {
    OK = 0,
    NO_CHANGE,
    INVALID_SOURCE_FRAME,
    RESOLVE_FAILED,
    TEMPORAL_REJECTED,
    QUEUE_REJECTED,
};

struct MidiCcGlobalFrameResult {
    MidiCcGlobalFrameStatus status = MidiCcGlobalFrameStatus::INVALID_SOURCE_FRAME;
    core::state::shared::MidiCcResolveStatus resolveStatus =
        core::state::shared::MidiCcResolveStatus::INVALID_INPUT;
    core::sequencer::RealtimeMidiQueueBatchStatus queueStatus =
        core::sequencer::RealtimeMidiQueueBatchStatus::INVALID_INPUT;
    uint16_t candidateCount = 0;
    uint16_t destinationCount = 0;
    uint16_t conflictCount = 0;
    uint16_t noRouteCount = 0;
    uint16_t eligibleEmissionCount = 0;
    uint16_t queuedEmissionCount = 0;

    [[nodiscard]] bool ok() const {
        return status == MidiCcGlobalFrameStatus::OK ||
               status == MidiCcGlobalFrameStatus::NO_CHANGE;
    }
};

struct MidiCcGlobalFrameDiagnostics {
    uint32_t publishedPersistentFrameCount = 0;
    uint32_t publishedLaneFrameCount = 0;
    uint32_t resolvedLiveFrameCount = 0;
    uint32_t queueRejectedFrameCount = 0;
    uint32_t temporalRejectedFrameCount = 0;
    uint32_t stagedAuthorTransitionCount = 0;
    uint32_t committedDeadlineGroupCount = 0;
    uint32_t trackInvalidationCount = 0;
    uint32_t laneGenerationInvalidationCount = 0;
    uint32_t pendingRemovalRetryCount = 0;
    uint32_t capturedProjectTriggerEventCount = 0;
    uint32_t projectTriggerEventOverflowCount = 0;
};

/**
 * Singular Gate 8 classic-CC arbitration and queue bridge.
 *
 * Producers publish complete, immutable frames. Manual entries are ordinary
 * persistent LIVE_MANUAL candidates: they remain present until Gate 7 Resume Auto
 * removes them. The single consumer composes Manual/Macro + all 64 lane holds,
 * diffs stable authors, schedules their transitions, and arbitrates only when
 * each transition reaches its physical deadline. Resolved events are appended
 * transactionally; delayed trajectories are never globally replaced.
 *
 * Source publication is triple-buffered under InterruptGuard. Resolution and
 * queue commit have one single LIVE owner (SequencerRuntimeService, including
 * its timer ISR lane). Preview uses the pure resolver with separate caller-owned
 * storage; it MUST NOT enter this coordinator. Each source has exactly one
 * producer. Triple buffering prevents a second publish from overwriting a frame
 * while the consumer or UI reader is copying it. The object is intentionally
 * large and MUST be allocated through makeExtmemUnique.
 */
class MidiCcGlobalFrameCoordinator final
    : private core::sequencer::RealtimeMidiQueueLifecycleObserver {
public:
    using Telemetry = core::state::shared::MidiCcResolutionTelemetry;
    using PersistentAuthorProducer = bool (*)(
        void* context,
        core::state::shared::MidiCcCandidate* destination,
        uint16_t capacity,
        uint16_t& written
    );

    /**
     * Stable zero-copy view of the last committed LIVE telemetry frame.
     *
     * At most one view may be active. A nested acquisition returns an invalid
     * view instead of stealing the reader slot. Keeping a view alive never
     * blocks the realtime writer: the other two telemetry frames remain
     * available for publication.
     */
    class TelemetryReadView final {
    public:
        TelemetryReadView() = default;
        ~TelemetryReadView();

        TelemetryReadView(const TelemetryReadView&) = delete;
        TelemetryReadView& operator=(const TelemetryReadView&) = delete;
        TelemetryReadView(TelemetryReadView&& other) noexcept;
        TelemetryReadView& operator=(TelemetryReadView&& other) noexcept;

        [[nodiscard]] explicit operator bool() const { return telemetry_ != nullptr; }
        [[nodiscard]] const Telemetry* get() const { return telemetry_; }
        [[nodiscard]] const Telemetry& operator*() const { return *telemetry_; }
        [[nodiscard]] const Telemetry* operator->() const { return telemetry_; }

    private:
        friend class MidiCcGlobalFrameCoordinator;

        TelemetryReadView(
            const MidiCcGlobalFrameCoordinator& owner,
            const Telemetry& telemetry,
            uint8_t index
        );
        void release_();

        const MidiCcGlobalFrameCoordinator* owner_ = nullptr;
        const Telemetry* telemetry_ = nullptr;
        uint8_t index_ = 0xFF;
    };

    static constexpr uint8_t OUTPUT_PORT = 0;

    explicit MidiCcGlobalFrameCoordinator(
        core::sequencer::RealtimeMidiQueue& queue,
        uint8_t outputPort = OUTPUT_PORT
    );
    ~MidiCcGlobalFrameCoordinator() override;

    MidiCcGlobalFrameCoordinator(const MidiCcGlobalFrameCoordinator&) = delete;
    MidiCcGlobalFrameCoordinator& operator=(const MidiCcGlobalFrameCoordinator&) = delete;

    /**
     * Deterministic complete-frame publication primitive.
     *
     * Production adapters normally use publishPersistentAuthorsGenerated() to
     * avoid an intermediate frame. This direct bounded form remains useful to
     * domain adapters and tests that already own immutable candidate storage;
     * it has the same validation, deduplication, and transactional publication
     * contract as the generated form.
     */
    bool publishPersistentAuthors(
        const core::state::shared::MidiCcCandidate* candidates,
        size_t candidateCount
    );
    bool publishPersistentAuthorsGenerated(
        PersistentAuthorProducer producer,
        void* context
    );
    /**
     * Replaces or appends one bounded persistent author slot.
     * Used by immediate LIVE_MANUAL input so the underlying Base author stays
     * present and keeps advancing until the next complete producer frame.
     */
    bool upsertPersistentAuthor(
        const core::state::shared::MidiCcCandidate& candidate,
        uint16_t& publishedCandidateCount
    );
    bool publishSequencerLanes(
        const core::sequencer::SequencerCcLaneRuntimeFrame& frame
    );

    /** Singular sequencer-clock publication consumed by Project control. */
    void publishProjectControlClock(
        uint32_t sequencerTick,
        bool playing,
        uint32_t nowUs,
        uint32_t sequencerTickPeriodUs
    );
    [[nodiscard]] core::state::modulation::ProjectControlTimeSnapshot
        projectControlTimeSnapshot() const;

    [[nodiscard]] bool needsLiveResolution(uint32_t nowUs) const;
    MidiCcGlobalFrameResult resolveLive(
        uint32_t nowUs,
        const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
        uint32_t tickPeriodUs = 0U,
        bool allowPredictiveLookahead = false
    );

    /** Cancel stale generations and remove this Track from arbitration now. */
    void invalidateTrack(uint8_t trackIndex);

    /** True when physically dispatched Note edges await Project evaluation. */
    [[nodiscard]] bool hasPendingProjectModulationTriggers() const;

    /**
     * Single-consumer drain into caller-owned EXTMEM scratch. Events retain
     * physical dispatch order and are consumed exactly once.
     */
    uint16_t drainProjectModulationTriggers(
        core::state::modulation::ProjectModulationTriggerFrame& out
    );

    /**
     * A transport stop deliberately drops pending realtime events without
     * changing the authored frame or the last physically dispatched values.
     * Queue-removal retry is deferred, not discarded: identical Lane holds
     * remain silent while stopped, then the first resume clock retries any
     * accepted-but-undispatched intent exactly once. Persistent Macro source
     * revisions remain independently resolvable while transport is stopped.
     */
    void discardPendingRetryForTransportStop();

    void resetProject();

    [[nodiscard]] TelemetryReadView readTelemetry() const;
    [[nodiscard]] const MidiCcGlobalFrameDiagnostics& diagnostics() const {
        return diagnostics_;
    }

private:
    struct DesiredValue {
        core::state::shared::MidiCcDestinationIdentity identity{};
        uint8_t value = 0;
    };

    struct LaneAuthorFrame {
        uint32_t revision = 0;
        uint8_t candidateCount = 0;
        std::array<
            core::state::shared::MidiCcCandidate,
            core::sequencer::SequencerCcLaneRuntimeFrame::MAX_CANDIDATES
        > candidates{};
        std::array<uint16_t, core::sequencer::SequencerCcLaneRuntime::ADDRESS_COUNT>
            lifecycleGenerations{};
        uint64_t predictiveAuthorMask = 0U;
    };

    struct TemporalAuthorState {
        core::state::shared::MidiCcCandidate candidate{};
        uint16_t lifecycleGeneration = 0U;
        bool present = false;
    };

    bool captureCombinedCandidates_();
    MidiCcGlobalFrameResult resolve_(
        uint32_t nowUs,
        const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
        uint32_t tickPeriodUs,
        bool allowPredictiveLookahead
    );
    bool stageLogicalFrame_(
        uint32_t nowUs,
        const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
        uint32_t tickPeriodUs,
        bool allowPredictiveLookahead,
        MidiCcGlobalFrameResult& result
    );
    bool processDueGroups_(uint32_t nowUs, MidiCcGlobalFrameResult& result);
    bool resolveEffective_(uint32_t deadlineUs, MidiCcGlobalFrameResult& result);
    void rollbackEffectiveTransitions_(size_t count);
    void clearTrackAuthorStates_(uint8_t trackIndex);
    void synchronizeStoppedLaneLogicalState_();
    uint32_t deadlineForAuthor_(
        const core::state::shared::MidiCcAuthor& author,
        uint32_t nowUs,
        const core::sequencer::ProjectTrackRuntimeSnapshot& projectTracks,
        uint32_t tickPeriodUs,
        bool allowPredictiveLookahead
    ) const;
    void releaseTelemetryReader_(uint8_t index) const;
    bool plannedValueMatches_(const DesiredValue& desired) const;
    void publishDesiredAndPruneDispatched_(
        const std::array<DesiredValue,
                         core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS>&
            desired,
        uint16_t desiredCount
    );
    static uint8_t trackForAuthor_(
        const core::state::shared::MidiCcAuthor& author
    );
    void enqueueProjectModulationTrigger_(
        const core::state::modulation::ProjectModulationTriggerEvent& event
    );

    void onRealtimeMidiEventEnqueued(
        const core::sequencer::RealtimeMidiEvent& event
    ) override;
    void onRealtimeMidiEventRemoved(
        const core::sequencer::RealtimeMidiEvent& event,
        core::sequencer::RealtimeMidiQueueLifecycleReason reason
    ) override;
    void onRealtimeMidiEventDispatched(
        const core::sequencer::RealtimeMidiEvent& event
    ) override;

    core::sequencer::RealtimeMidiQueue& queue_;
    uint8_t output_port_ = OUTPUT_PORT;
    static constexpr uint8_t SOURCE_FRAME_COUNT = 3;
    static constexpr uint8_t TELEMETRY_FRAME_COUNT = 3;
    static constexpr uint8_t NO_SOURCE_READER = 0xFF;

    std::array<MidiCcPersistentAuthorFrame, SOURCE_FRAME_COUNT>
        persistent_frames_{};
    std::array<LaneAuthorFrame, SOURCE_FRAME_COUNT> lane_frames_{};
    volatile uint8_t active_persistent_index_ = 0;
    volatile uint8_t active_lane_index_ = 0;
    volatile uint8_t reading_persistent_index_ = NO_SOURCE_READER;
    volatile uint8_t reading_lane_index_ = NO_SOURCE_READER;
    uint32_t next_persistent_revision_ = 1;
    uint32_t next_lane_revision_ = 1;
    uint32_t last_live_persistent_revision_ = 0;
    uint32_t last_live_lane_revision_ = 0;
    uint32_t captured_persistent_revision_ = 0;
    uint32_t captured_lane_revision_ = 0;
    std::array<uint16_t, core::sequencer::SequencerCcLaneRuntime::ADDRESS_COUNT>
        captured_lane_lifecycle_generations_{};
    uint64_t captured_lane_predictive_author_mask_ = 0U;
    std::array<uint16_t, core::sequencer::SequencerCcLaneRuntime::ADDRESS_COUNT>
        logical_lane_lifecycle_generations_{};
    std::array<
        core::state::shared::MidiCcCandidate,
        core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES
    > combined_candidates_{};
    uint16_t combined_candidate_count_ = 0;
    std::array<uint16_t,
               core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES>
        target_author_slots_{};
    std::array<TemporalAuthorState,
               core::sequencer::TemporalMidiCcAuthorSpool::AUTHOR_SLOT_COUNT>
        logical_authors_{};
    std::array<TemporalAuthorState,
               core::sequencer::TemporalMidiCcAuthorSpool::AUTHOR_SLOT_COUNT>
        effective_authors_{};
    std::array<uint16_t,
               core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES>
        effective_active_slots_{};
    uint16_t effective_active_slot_count_ = 0U;
    std::array<uint16_t,
               core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES>
        effective_active_slots_rollback_{};
    uint16_t effective_active_slot_count_rollback_ = 0U;
    std::array<uint16_t,
               core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES>
        logical_active_slots_{};
    uint16_t logical_active_slot_count_ = 0U;
    std::array<uint16_t,
               core::sequencer::TemporalMidiCcAuthorSpool::AUTHOR_SLOT_COUNT>
        target_seen_generation_{};
    uint16_t next_target_seen_generation_ = 1U;
    core::sequencer::TemporalMidiCcAuthorSpool temporal_spool_{};
    std::array<core::sequencer::TemporalMidiCcAuthorTransition,
               core::sequencer::TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS>
        transition_scratch_{};
    std::array<TemporalAuthorState,
               core::sequencer::TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS>
        transition_rollback_states_{};
    std::array<uint16_t,
               core::sequencer::TemporalMidiCcAuthorSpool::MAX_DUE_TRANSITIONS>
        transition_rollback_slots_{};
    bool source_restage_required_ = false;
    bool effective_dirty_ = false;
    std::array<Telemetry, TELEMETRY_FRAME_COUNT>
        telemetry_frames_{};
    volatile uint8_t published_telemetry_index_ = 0;
    mutable volatile uint8_t reading_telemetry_index_ = NO_SOURCE_READER;
    std::array<
        DesiredValue,
        core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS
    > desired_values_{};
    uint16_t desired_value_count_ = 0;
    bool planned_values_valid_ = true;
    std::array<
        DesiredValue,
        core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS
    > dispatched_values_{};
    uint16_t dispatched_value_count_ = 0;
    std::array<
        core::sequencer::RealtimeMidiEvent,
        core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS
    > pending_events_{};
    std::array<
        DesiredValue,
        core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS
    > pending_desired_values_{};
    bool retry_requested_ = false;
    // Set only at the explicit Stop boundary when queue removal invalidated a
    // planned CC set. While armed, desired_values_ is a logical stop-time
    // fence: it suppresses Lane fallback/re-emission but remains distinct from
    // dispatched_values_. Resume invalidates that fence and requests one
    // physical reconciliation against what was actually dispatched.
    bool transport_retry_deferred_until_resume_ = false;
    bool replacing_pending_controls_ = false;
    bool lifecycle_attached_ = false;
    core::state::modulation::ProjectControlTimeSnapshot control_time_{};
    uint32_t control_tick_started_us_ = 0;
    uint32_t last_control_sequencer_tick_ = 0;
    bool control_clock_initialized_ = false;
    static constexpr uint16_t PROJECT_TRIGGER_RING_CAPACITY =
        core::state::modulation::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY;
    static constexpr uint16_t PROJECT_TRIGGER_RING_MASK =
        PROJECT_TRIGGER_RING_CAPACITY - 1U;
    std::array<
        core::state::modulation::ProjectModulationTriggerEvent,
        PROJECT_TRIGGER_RING_CAPACITY
    > project_trigger_events_{};
    std::atomic<uint16_t> project_trigger_write_sequence_{0U};
    std::atomic<uint16_t> project_trigger_read_sequence_{0U};
    std::atomic<uint16_t> project_trigger_overflow_sequence_{0U};
    uint16_t project_trigger_last_drained_overflow_sequence_ = 0U;
    MidiCcGlobalFrameDiagnostics diagnostics_{};
};

static_assert(std::is_trivially_copyable_v<MidiCcPersistentAuthorFrame>);
static_assert(!std::is_copy_constructible_v<
              MidiCcGlobalFrameCoordinator::TelemetryReadView>);
static_assert(std::is_nothrow_move_constructible_v<
              MidiCcGlobalFrameCoordinator::TelemetryReadView>);
static_assert(
    (core::state::modulation::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY &
     (core::state::modulation::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY - 1U)) ==
    0U
);
static_assert(std::atomic<uint16_t>::is_always_lock_free);
// Production ownership is one EXTMEM allocation; the realtime lane keeps only
// the pointer in RAM2. The generous bound prevents accidental RAM1 placement
// while still catching unbounded growth.
static_assert(sizeof(MidiCcGlobalFrameCoordinator) <= 512U * 1024U);

}  // namespace core::handler
