#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerCcLaneRuntime.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"

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
    uint32_t pendingRemovalRetryCount = 0;
};

/**
 * Singular Gate 8 classic-CC arbitration and queue bridge.
 *
 * Producers publish complete, immutable frames. Manual entries are ordinary
 * persistent LIVE_MANUAL candidates: they remain present until Gate 7 Resume Auto
 * removes them. The single consumer composes Manual/Macro + all 64 lane holds,
 * resolves once, and atomically replaces pending CC in RealtimeMidiQueue.
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

    bool publishPersistentAuthors(
        const core::state::shared::MidiCcCandidate* candidates,
        size_t candidateCount
    );
    bool publishPersistentAuthorsGenerated(
        PersistentAuthorProducer producer,
        void* context
    );
    /**
     * Replaces one author in the currently published complete frame.
     * Returns false without publishing when that stable author is absent, so
     * callers can rebuild from their current structural context instead of
     * accidentally retaining authors from a stale page.
     */
    bool replacePersistentAuthor(
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

    [[nodiscard]] bool needsLiveResolution() const;
    MidiCcGlobalFrameResult resolveLive(uint32_t deadlineUs);

    /**
     * A transport stop deliberately drops pending realtime events without
     * changing the authored frame or the last physically dispatched values.
     * Suppress only the queue-removal retry created by that drop: holds remain
     * authoritative and resume can re-evaluate them on its next musical tick.
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
    };

    bool captureCombinedCandidates_();
    MidiCcGlobalFrameResult resolve_(uint32_t deadlineUs);
    void releaseTelemetryReader_(uint8_t index) const;
    bool dispatchedValueMatches_(const DesiredValue& desired) const;
    void publishDesiredAndPruneDispatched_(
        const std::array<DesiredValue,
                         core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS>&
            desired,
        uint16_t desiredCount
    );
    static uint8_t trackForAuthor_(
        const core::state::shared::MidiCcAuthor& author
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
    std::array<
        core::state::shared::MidiCcCandidate,
        core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES
    > combined_candidates_{};
    uint16_t combined_candidate_count_ = 0;
    std::array<Telemetry, TELEMETRY_FRAME_COUNT>
        telemetry_frames_{};
    volatile uint8_t published_telemetry_index_ = 0;
    mutable volatile uint8_t reading_telemetry_index_ = NO_SOURCE_READER;
    std::array<
        DesiredValue,
        core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS
    > desired_values_{};
    uint16_t desired_value_count_ = 0;
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
    bool replacing_pending_controls_ = false;
    bool lifecycle_attached_ = false;
    core::state::modulation::ProjectControlTimeSnapshot control_time_{};
    uint32_t control_tick_started_us_ = 0;
    uint32_t last_control_sequencer_tick_ = 0;
    bool control_clock_initialized_ = false;
    MidiCcGlobalFrameDiagnostics diagnostics_{};
};

static_assert(std::is_trivially_copyable_v<MidiCcPersistentAuthorFrame>);
static_assert(!std::is_copy_constructible_v<
              MidiCcGlobalFrameCoordinator::TelemetryReadView>);
static_assert(std::is_nothrow_move_constructible_v<
              MidiCcGlobalFrameCoordinator::TelemetryReadView>);
static_assert(sizeof(MidiCcGlobalFrameCoordinator) <= 48U * 1024U);

}  // namespace core::handler
