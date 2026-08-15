#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/NoteScheduler.hpp>
#include <oc/note/sequencer/SequencerEvent.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/DrumPatternState.hpp"

namespace core::sequencer {

struct DrumPlaybackDiagnostics {
    bool schedulerCapacityExceeded = false;
    bool sinkRejectedEvent = false;
    bool noteBudgetExceeded = false;
    bool depthLimitReached = false;
    uint32_t schedulerCapacityExceededCount = 0U;
    uint32_t sinkRejectedEventCount = 0U;
    uint32_t noteBudgetExceededCount = 0U;
    uint32_t depthLimitReachedCount = 0U;

    void reset() { *this = {}; }
};

struct DrumPlaybackTelemetry {
    DrumPlaybackDiagnostics diagnostics{};
    std::array<uint8_t, core::state::sequencer::DRUM_MAX_LANES>
        laneSteps{};
    std::array<uint8_t, core::state::sequencer::DRUM_MAX_LANES>
        lanePhaseQ8{};
    std::array<uint8_t, core::state::sequencer::DRUM_MAX_LANES>
        laneTicksPerStep{};
    std::array<uint8_t, core::state::sequencer::DRUM_MAX_LANES>
        laneDecisionSteps{};
    uint16_t laneValidMask = 0U;
    uint16_t laneDecisionValidMask = 0U;
    uint16_t laneDecisionPlayedMask = 0U;
    uint32_t transportTick = 0U;
    uint32_t tickAnchorUs = 0U;
    uint32_t tickPeriodUs = 0U;
    bool playing = false;

    void reset();
};

/** Cache key for the non-realtime, viewport-scoped advanced-content preview. */
struct DrumResolvedPageSignature {
    const core::state::sequencer::DrumPatternRuntimeSnapshot* pattern = nullptr;
    const oc::note::sequencer::StepSequencerGraph* graph = nullptr;
    std::array<
        uint32_t,
        core::state::sequencer::DrumResolvedPageProjection::VISIBLE_LANES
    > laneCycleIndices{};
    uint32_t patternRevision = 0U;
    uint32_t runSeed = 0U;
    uint8_t page = 0U;
    uint8_t laneWindowStart = 0U;
    bool playing = false;

    [[nodiscard]] bool matches(
        const DrumResolvedPageSignature& other
    ) const {
        return pattern == other.pattern && graph == other.graph &&
            laneCycleIndices == other.laneCycleIndices &&
            patternRevision == other.patternRevision &&
            runSeed == other.runSeed && page == other.page &&
            laneWindowStart == other.laneWindowStart &&
            playing == other.playing;
    }
};

/**
 * One hot scheduler for every lane in a Drum Track.
 *
 * Lane phase is derived from the shared absolute transport tick, so different
 * lengths and divisions remain deterministic without one heavyweight melodic
 * engine per lane. The update path performs no allocation.
 */
class DrumPlaybackEngine {
public:
    explicit DrumPlaybackEngine(
        oc::note::sequencer::ISequencerEventSink& eventSink
    );

    void setPattern(
        const core::state::sequencer::DrumPatternRuntimeSnapshot* pattern,
        const oc::note::sequencer::StepSequencerGraph* graph,
        uint8_t midiChannel
    );
    void reset();
    void update(
        uint32_t tick,
        bool playing,
        uint32_t nowUs = 0U,
        uint32_t tickPeriodUs = 0U
    );

    [[nodiscard]] bool isPlaying() const { return playing_; }
    [[nodiscard]] const DrumPlaybackTelemetry& telemetry() const {
        return telemetry_;
    }

    /**
     * Capture/build are main-loop authoring-preview operations. They never run
     * in the timer update path and allocate no memory.
     */
    [[nodiscard]] DrumResolvedPageSignature captureResolvedPageSignature(
        uint8_t page,
        uint8_t laneWindowStart
    ) const;
    void buildResolvedPageProjection(
        const DrumResolvedPageSignature& signature,
        core::state::sequencer::DrumResolvedPageProjection& out
    ) const;

private:
    static constexpr uint32_t MAX_CATCH_UP_TICKS = 48U;

    // One authored root may expand to sixteen notes. 128 queued edges cover a
    // useful multi-lane burst while retaining a small, explicit RAM2 budget.
    // Capacity overflow is fail-closed (panic + diagnostic), never partial.
    using Scheduler = oc::note::sequencer::BoundedNoteScheduler<128U, 16U>;

    void start_(uint32_t tick);
    void stop_(uint32_t tick);
    void processTick_(uint32_t tick);
    bool triggerDueLaneStep_(uint8_t lane, uint32_t tick);
    bool scheduleLaneStep_(
        uint8_t lane,
        uint32_t ordinal,
        uint32_t onTick
    );
    bool emitAllNotesOff_(uint32_t tick);
    bool processDue_(uint32_t tick);
    bool scheduleNote_(
        uint32_t onTick,
        uint32_t offTick,
        uint8_t channel,
        uint8_t note,
        uint8_t velocity
    );
    void clearPendingNotes_();
    void refreshLanePhases_(
        uint32_t tick,
        uint32_t nowUs,
        uint32_t tickPeriodUs
    );
    void publishLaneDecision_(uint8_t lane, uint8_t step, bool played);
    void clearLaneDecision_(uint8_t lane);

    static uint8_t ticksPerStep_(uint8_t stepsPerBeat);
    static int32_t nudgeTickOffset_(int8_t nudge, uint8_t ticksPerStep);
    static uint32_t probabilityHash_(
        uint32_t runSeed,
        uint8_t lane,
        uint32_t cycleIndex,
        uint8_t step
    );

    const core::state::sequencer::DrumPatternRuntimeSnapshot* pattern_ =
        nullptr;
    const oc::note::sequencer::StepSequencerGraph* graph_ = nullptr;
    oc::note::sequencer::ISequencerEventSink& event_sink_;
    Scheduler scheduler_{};
    std::array<uint32_t, core::state::sequencer::DRUM_MAX_LANES>
        last_triggered_ordinals_{};
    DrumPlaybackTelemetry telemetry_{};
    uint8_t midi_channel_ = 0U;
    uint32_t last_tick_ = 0U;
    uint32_t run_seed_ = 0U;
    uint32_t pattern_revision_ = 0U;
    bool playing_ = false;
    bool last_tick_valid_ = false;
    bool pattern_change_pending_ = false;
};

static_assert(sizeof(DrumPlaybackEngine) < 2048U);

}  // namespace core::sequencer
