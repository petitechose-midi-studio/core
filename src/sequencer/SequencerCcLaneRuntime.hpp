#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>

#include "state/sequencer/SequencerCcLaneRouting.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::sequencer {

/** Select whether one input describes the audible tick or a future horizon. */
enum class SequencerCcLaneEmissionMode : uint8_t {
    CURRENT_TICK = 0,
    PREDICTIVE_LOOKAHEAD,
};

/** Immutable input for one Track at one scheduler tick. */
struct SequencerCcLaneTrackRuntimeInput {
    const core::state::sequencer::SequencerCcLaneBank* lanes = nullptr;
    core::state::sequencer::SequencerCcTrackRoute route{};
    uint8_t step = 0;
    uint8_t patternLength = 128;
    uint8_t tickInStep = 0;
    uint8_t ticksPerStep = 1;
    uint32_t playbackOrdinal = 0;
    oc::note::sequencer::StepSequencerPlaybackRegion playbackRegion{0, 0, 0, 0};
    SequencerCcLaneEmissionMode emissionMode =
        SequencerCcLaneEmissionMode::CURRENT_TICK;
    // Exclusive lower bound of a predictive window: the ordinal currently
    // audible to the user. Ignored in CURRENT_TICK mode.
    uint32_t lookaheadStartOrdinal = 0;
    bool enabled = false;
    bool muted = false;
    bool stepTriggered = false;
    // A staged Track transaction keeps its previous audible generation until
    // the activation boundary. The runtime republishes existing holds without
    // observing the newly published Pattern payload.
    bool frozen = false;
};

enum class SequencerCcLaneRuntimeStatus : uint8_t {
    OK = 0,
    INVALID_INPUT,
    CAPACITY_EXCEEDED,
};

struct SequencerCcLaneRuntimeContribution {
    core::state::sequencer::SequencerCcLaneAddress address{};
    core::state::shared::MidiCcDestination destination{};
    uint8_t heldValue = 0;
    uint16_t lifecycleGeneration = 0;
    bool authoredEventThisTick = false;
    bool valueChangedThisTick = false;
    bool routeRetargetedThisTick = false;
};

/** Complete bounded Sequencer-lane portion of one global resolver frame. */
struct SequencerCcLaneRuntimeFrame {
    static constexpr uint8_t MAX_CANDIDATES = 64;

    SequencerCcLaneRuntimeStatus status = SequencerCcLaneRuntimeStatus::OK;
    uint8_t candidateCount = 0;
    uint8_t authoredEventCount = 0;
    uint8_t routeRetargetCount = 0;
    uint8_t noRouteCount = 0;
    uint8_t suppressedTrackCount = 0;
    std::array<
        core::state::shared::MidiCcCandidate,
        MAX_CANDIDATES
    > candidates{};
    std::array<SequencerCcLaneRuntimeContribution, MAX_CANDIDATES>
        contributions{};
    // Complete stable-address generation projection, including empty lanes.
    // The temporal scheduler uses it to cancel a pasted/restored generation's
    // future mutations before diffing the new complete author frame.
    std::array<uint16_t, 64> lifecycleGenerations{};
    // One bit per stable Lane author. A set bit means this frame contains the
    // successfully projected future state for that author (including an
    // intentional future absence). The deadline coordinator must only apply a
    // negative-delay residual to these authors; current-frame fallbacks are
    // due immediately.
    uint64_t predictiveAuthorMask = 0U;

    [[nodiscard]] bool ok() const {
        return status == SequencerCcLaneRuntimeStatus::OK;
    }
};

/**
 * Realtime-safe stepped-hold evaluator for all 16 Patterns.
 *
 * It performs no allocation. A frame is transactional: malformed input or
 * capacity failure publishes neither partial candidates nor partial held state.
 * Transport stop and Track mute/disable deliberately retain held state and
 * never synthesize a reset. Firmware ownership MUST use makeExtmemUnique: the
 * double transactional scratch is intentionally kept out of scarce RAM1.
 */
class SequencerCcLaneRuntime final {
public:
    static constexpr uint8_t TRACK_COUNT = 16;
    static constexpr uint8_t LANE_COUNT =
        core::state::sequencer::SequencerCcLaneBank::MAX_LANES;
    static constexpr uint8_t ADDRESS_COUNT = TRACK_COUNT * LANE_COUNT;
    static constexpr uint32_t MAX_LOOKAHEAD_ORDINAL_DELTA = 128U;

    using Inputs =
        std::array<SequencerCcLaneTrackRuntimeInput, TRACK_COUNT>;

    SequencerCcLaneRuntime();

    void resetProject();

    /**
     * Seed the complete predictive scratch from the audible runtime in one
     * bounded hot-path copy. This allows all negative-delay Tracks to be
     * projected by one buildMusicalTickFrame() pass per scheduler tick.
     */
    [[nodiscard]] bool seedFrom(const SequencerCcLaneRuntime& source);

    /**
     * Build the complete lane candidate frame at one musical scheduler tick.
     *
     * The integration MUST call this only when the transport clock advances to
     * a new musical tick, never at the 1 kHz app/timer polling cadence. That is
     * what makes an inherited route retarget occur exactly on the next tick.
     * `playing=false` yields an empty frame and preserves hold/route state.
     */
    SequencerCcLaneRuntimeStatus buildMusicalTickFrame(
        const Inputs& inputs,
        bool playing,
        SequencerCcLaneRuntimeFrame& out
    );

    [[nodiscard]] bool hasHeldValue(uint8_t track, uint8_t lane) const;
    [[nodiscard]] uint8_t heldValue(uint8_t track, uint8_t lane) const;

private:
    struct LaneState {
        core::state::shared::MidiCcDestination lastDestination{};
        uint16_t lifecycleGeneration = 0;
        uint8_t heldValue = 0;
        uint8_t sourceStep = 0;
        bool occupiedObserved = false;
        bool hasHeldValue = false;
        bool hasResolvedDestination = false;
    };

    struct TrackProjectionState {
        oc::note::sequencer::StepSequencerPlaybackRegion region{0, 0, 0, 0};
        bool observed = false;
    };

    static constexpr uint8_t stateIndex_(uint8_t track, uint8_t lane) {
        return static_cast<uint8_t>(track * LANE_COUNT + lane);
    }

    std::array<LaneState, ADDRESS_COUNT> states_{};
    std::array<TrackProjectionState, TRACK_COUNT> track_projection_states_{};
    // Transactional scratch keeps both output and state unchanged on failure.
    std::array<LaneState, ADDRESS_COUNT> pending_states_{};
    std::array<TrackProjectionState, TRACK_COUNT> pending_track_projection_states_{};
    SequencerCcLaneRuntimeFrame pending_frame_{};
};

static_assert(SequencerCcLaneRuntime::ADDRESS_COUNT == 64U);
static_assert(std::is_trivially_copyable_v<SequencerCcLaneRuntimeFrame>);
static_assert(sizeof(SequencerCcLaneRuntimeFrame) <= 1792U);
static_assert(sizeof(SequencerCcLaneRuntime) <= 4096U);

}  // namespace core::sequencer
