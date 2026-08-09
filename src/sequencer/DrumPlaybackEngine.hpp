#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/NoteScheduler.hpp>
#include <oc/note/sequencer/SequencerEvent.hpp>

#include "state/sequencer/DrumPatternState.hpp"

namespace core::sequencer {

struct DrumPlaybackDiagnostics {
    bool schedulerCapacityExceeded = false;
    bool sinkRejectedEvent = false;
    uint32_t schedulerCapacityExceededCount = 0U;
    uint32_t sinkRejectedEventCount = 0U;

    void reset() { *this = {}; }
};

struct DrumPlaybackTelemetry {
    DrumPlaybackDiagnostics diagnostics{};
    std::array<uint8_t, core::state::sequencer::DRUM_MAX_LANES>
        laneSteps{};
    uint16_t laneValidMask = 0U;
    bool playing = false;

    void reset();
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
        uint8_t midiChannel
    );
    void reset();
    void update(uint32_t tick, bool playing);

    [[nodiscard]] bool isPlaying() const { return playing_; }
    [[nodiscard]] const DrumPlaybackTelemetry& telemetry() const {
        return telemetry_;
    }

private:
    static constexpr uint32_t MAX_CATCH_UP_TICKS = 48U;

    void start_(uint32_t tick);
    void stop_(uint32_t tick);
    void processTick_(uint32_t tick);
    void triggerDueLaneStep_(uint8_t lane, uint32_t tick);
    bool scheduleLaneStep_(
        uint8_t lane,
        uint32_t ordinal,
        uint32_t onTick
    );
    bool emitAllNotesOff_(uint32_t tick);
    bool processDue_(uint32_t tick);

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
    oc::note::sequencer::ISequencerEventSink& event_sink_;
    oc::note::sequencer::NoteScheduler scheduler_{};
    std::array<uint32_t, core::state::sequencer::DRUM_MAX_LANES>
        last_triggered_ordinals_{};
    DrumPlaybackTelemetry telemetry_{};
    uint8_t midi_channel_ = 0U;
    uint32_t last_tick_ = 0U;
    uint32_t run_seed_ = 0U;
    bool playing_ = false;
    bool last_tick_valid_ = false;
};

}  // namespace core::sequencer
