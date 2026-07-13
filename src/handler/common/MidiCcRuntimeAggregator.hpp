#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include <oc/api/MidiAPI.hpp>

#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::handler {

/**
 * Result of publishing one complete destination-resolution frame.
 *
 * eligibleEmissionCount is the number of valid Live destinations produced by
 * the resolver. sentCount is the smaller number of MIDI messages actually
 * emitted after unchanged-value suppression.
 */
struct MidiCcRuntimePublishResult {
    core::state::shared::MidiCcResolveStatus status =
        core::state::shared::MidiCcResolveStatus::INVALID_INPUT;
    uint16_t candidateCount = 0;
    uint16_t destinationCount = 0;
    uint16_t conflictCount = 0;
    uint16_t noRouteCount = 0;
    uint16_t eligibleEmissionCount = 0;
    uint16_t sentCount = 0;

    [[nodiscard]] bool ok() const {
        return status == core::state::shared::MidiCcResolveStatus::OK;
    }
};

/**
 * Bounded runtime owner for the shared classic-CC destination resolver.
 *
 * Callers open one complete frame, add every active author, then publish it.
 * The resolver is invoked exactly once for that frame and MIDI is emitted at
 * most once per resolved destination. Typed ingress methods keep Gate 6 source
 * classes explicit; Sequencer lane ingress is intentionally available before
 * Gate 8 wires the lane runtime.
 *
 * The object performs no allocation. It is deliberately large because it owns
 * two telemetry frames and two sent-value caches so failed frames never expose
 * partial state and successful frames require no hot-path telemetry copy. The
 * standalone assembly must therefore allocate the shared instance in EXTMEM.
 */
class MidiCcRuntimeAggregator final {
public:
    static constexpr uint8_t DEFAULT_OUTPUT_PORT = 0;

    explicit MidiCcRuntimeAggregator(
        oc::api::MidiAPI& midi,
        uint8_t outputPort = DEFAULT_OUTPUT_PORT
    );

    MidiCcRuntimeAggregator(const MidiCcRuntimeAggregator&) = delete;
    MidiCcRuntimeAggregator& operator=(const MidiCcRuntimeAggregator&) = delete;

    void reset();

    /** Discards any uncommitted frame and starts a new complete frame. */
    void beginFrame(core::state::shared::MidiCcResolutionMode mode);

    core::state::shared::MidiCcResolveStatus addLiveManual(
        const core::state::shared::MidiCcDestination& destination,
        uint16_t stableAddress,
        uint8_t localValue
    );
    core::state::shared::MidiCcResolveStatus addSequencerCcLane(
        const core::state::shared::MidiCcDestination& destination,
        uint16_t stableAddress,
        uint8_t localValue
    );
    core::state::shared::MidiCcResolveStatus addMacroComputed(
        const core::state::shared::MidiCcDestination& destination,
        uint16_t stableAddress,
        uint8_t localValue
    );
    core::state::shared::MidiCcResolveStatus addMacroStatic(
        const core::state::shared::MidiCcDestination& destination,
        uint16_t stableAddress,
        uint8_t localValue
    );

    /**
     * Atomically publishes telemetry and, for Live mode, changed MIDI values.
     * On every failure the previous telemetry and sent-value cache remain
     * unchanged and no MIDI is emitted.
     */
    MidiCcRuntimePublishResult publish();

    [[nodiscard]] const core::state::shared::MidiCcResolutionTelemetry& telemetry() const;
    [[nodiscard]] uint8_t outputPort() const { return output_port_; }
    [[nodiscard]] bool frameOpen() const { return frame_open_; }

private:
    struct SentDestinationValue {
        core::state::shared::MidiCcDestinationIdentity identity{};
        uint8_t value = 0;
    };

    core::state::shared::MidiCcResolveStatus addCandidate_(
        core::state::shared::MidiCcCandidateClass candidateClass,
        const core::state::shared::MidiCcDestination& destination,
        uint16_t stableAddress,
        uint8_t localValue
    );
    bool validForBoundOutput_(
        const core::state::shared::MidiCcCandidate& candidate
    ) const;

    oc::api::MidiAPI& midi_;
    uint8_t output_port_ = DEFAULT_OUTPUT_PORT;
    std::array<
        core::state::shared::MidiCcCandidate,
        core::state::shared::MidiCcResolutionTelemetry::MAX_CANDIDATES
    > candidates_{};
    std::array<core::state::shared::MidiCcResolutionTelemetry, 2> telemetry_frames_{};
    std::array<
        std::array<
            SentDestinationValue,
            core::state::shared::MidiCcResolutionTelemetry::MAX_DESTINATIONS
        >,
        2
    > sent_caches_{};
    std::array<uint16_t, 2> sent_cache_counts_{};
    uint16_t candidate_count_ = 0;
    core::state::shared::MidiCcResolutionMode frame_mode_ =
        core::state::shared::MidiCcResolutionMode::PREVIEW;
    core::state::shared::MidiCcResolveStatus frame_status_ =
        core::state::shared::MidiCcResolveStatus::OK;
    uint8_t published_telemetry_index_ = 0;
    uint8_t active_sent_cache_index_ = 0;
    bool frame_open_ = false;
};

static_assert(std::is_standard_layout_v<MidiCcRuntimePublishResult>);
static_assert(std::is_trivially_copyable_v<MidiCcRuntimePublishResult>);
static_assert(sizeof(MidiCcRuntimeAggregator) <= 24U * 1024U);

}  // namespace core::handler
