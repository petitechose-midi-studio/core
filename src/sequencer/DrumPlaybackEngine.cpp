#include "sequencer/DrumPlaybackEngine.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>

namespace core::sequencer {

namespace drum = core::state::sequencer;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;

namespace {

void incrementSaturating(uint32_t& value) {
    if (value != UINT32_MAX) ++value;
}

uint8_t clampChannel(uint8_t channel) {
    return channel > 15U ? 15U : channel;
}

}  // namespace

FLASHMEM void DrumPlaybackTelemetry::reset() {
    diagnostics.reset();
}

FLASHMEM DrumPlaybackEngine::DrumPlaybackEngine(
    oc::note::sequencer::ISequencerEventSink& eventSink
)
    : event_sink_(eventSink) {
    reset();
}

void DrumPlaybackEngine::setPattern(
    const drum::DrumPatternRuntimeSnapshot* pattern,
    uint8_t midiChannel
) {
    pattern_ = pattern;
    midi_channel_ = clampChannel(midiChannel);
}

FLASHMEM void DrumPlaybackEngine::reset() {
    scheduler_.clear();
    last_triggered_ordinals_.fill(UINT32_MAX);
    telemetry_.reset();
    last_tick_ = 0U;
    last_tick_valid_ = false;
    playing_ = false;
}

void DrumPlaybackEngine::update(uint32_t tick, bool playing) {
    if (!playing || pattern_ == nullptr || pattern_->laneCount == 0U) {
        if (playing_) stop_(tick);
        return;
    }

    if (!playing_) {
        start_(tick);
        processTick_(tick);
        return;
    }

    if (!last_tick_valid_ || tick < last_tick_ ||
        tick - last_tick_ > MAX_CATCH_UP_TICKS) {
        scheduler_.clear();
        last_triggered_ordinals_.fill(UINT32_MAX);
        processTick_(tick);
        return;
    }

    if (tick == last_tick_) {
        processDue_(tick);
        return;
    }

    for (uint32_t current = last_tick_ + 1U; current <= tick; ++current) {
        processTick_(current);
        if (current == UINT32_MAX) break;
    }
}

FLASHMEM void DrumPlaybackEngine::start_(uint32_t tick) {
    scheduler_.clear();
    last_triggered_ordinals_.fill(UINT32_MAX);
    ++run_seed_;
    if (run_seed_ == 0U) run_seed_ = 1U;
    playing_ = true;
    last_tick_ = tick;
    last_tick_valid_ = false;
}

FLASHMEM void DrumPlaybackEngine::stop_(uint32_t tick) {
    (void)emitAllNotesOff_(tick);
    scheduler_.clear();
    last_triggered_ordinals_.fill(UINT32_MAX);
    playing_ = false;
    last_tick_valid_ = false;
}

void DrumPlaybackEngine::processTick_(uint32_t tick) {
    const uint8_t laneCount = std::min<uint8_t>(
        pattern_->laneCount,
        drum::DRUM_MAX_LANES
    );
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        triggerDueLaneStep_(lane, tick);
    }
    (void)processDue_(tick);
    last_tick_ = tick;
    last_tick_valid_ = true;
}

void DrumPlaybackEngine::triggerDueLaneStep_(uint8_t lane, uint32_t tick) {
    const auto& source = pattern_->lanes[lane];
    const uint8_t length = std::max<uint8_t>(1U, source.length);
    const uint8_t ticksPerStep = ticksPerStep_(source.stepsPerBeat);
    const uint32_t baseOrdinal = tick / ticksPerStep;

    // A negative nudge can pull only the next ordinal into this base interval;
    // the current ordinal covers zero and positive nudges.
    for (uint8_t candidate = 0U; candidate < 2U; ++candidate) {
        const uint32_t ordinal = baseOrdinal + candidate;
        const uint8_t step = static_cast<uint8_t>(ordinal % length);
        const int32_t offset = nudgeTickOffset_(
            source.nudge[step],
            ticksPerStep
        );
        int64_t onTickSigned =
            static_cast<int64_t>(ordinal) * ticksPerStep + offset;
        if (onTickSigned < 0) onTickSigned = 0;
        const uint32_t onTick = static_cast<uint32_t>(onTickSigned);
        if (onTick != tick || last_triggered_ordinals_[lane] == ordinal) {
            continue;
        }
        last_triggered_ordinals_[lane] = ordinal;
        (void)scheduleLaneStep_(lane, ordinal, onTick);
    }
}

bool DrumPlaybackEngine::scheduleLaneStep_(
    uint8_t lane,
    uint32_t ordinal,
    uint32_t onTick
) {
    const auto& source = pattern_->lanes[lane];
    const uint8_t length = std::max<uint8_t>(1U, source.length);
    const uint8_t step = static_cast<uint8_t>(ordinal % length);
    if (!source.enabledMask.test(step) || source.gate[step] == 0U) {
        return true;
    }

    const uint8_t probability = std::min<uint8_t>(source.probability[step], 100U);
    if (probability == 0U) return true;
    const uint32_t cycle = ordinal / length;
    if (probability < 100U &&
        probabilityHash_(run_seed_, lane, cycle, step) % 100U >= probability) {
        return true;
    }

    const uint8_t ticksPerStep = ticksPerStep_(source.stepsPerBeat);
    uint32_t gateTicks =
        static_cast<uint32_t>(std::min<uint16_t>(
            source.gate[step],
            drum::DRUM_MAX_GATE_PERCENT
        )) * ticksPerStep / 100U;
    if (gateTicks == 0U) gateTicks = 1U;

    if (scheduler_.scheduleNote(
            onTick,
            onTick + gateTicks,
            midi_channel_,
            source.midiNote,
            source.velocity[step]
        )) {
        return true;
    }

    telemetry_.diagnostics.schedulerCapacityExceeded = true;
    incrementSaturating(
        telemetry_.diagnostics.schedulerCapacityExceededCount
    );
    (void)emitAllNotesOff_(onTick);
    scheduler_.clear();
    return false;
}

FLASHMEM bool DrumPlaybackEngine::emitAllNotesOff_(uint32_t tick) {
    SequencerEvent event{};
    event.tick = tick;
    event.type = SequencerEventType::AllNotesOff;
    if (event_sink_.emitSequencerEvent(event)) return true;
    telemetry_.diagnostics.sinkRejectedEvent = true;
    incrementSaturating(telemetry_.diagnostics.sinkRejectedEventCount);
    return false;
}

bool DrumPlaybackEngine::processDue_(uint32_t tick) {
    if (scheduler_.processUntil(tick, event_sink_)) return true;
    telemetry_.diagnostics.sinkRejectedEvent = true;
    incrementSaturating(telemetry_.diagnostics.sinkRejectedEventCount);
    (void)emitAllNotesOff_(tick);
    scheduler_.clear();
    return false;
}

uint8_t DrumPlaybackEngine::ticksPerStep_(uint8_t stepsPerBeat) {
    uint8_t safe = stepsPerBeat;
    if (safe == 0U) safe = drum::DRUM_DEFAULT_STEPS_PER_BEAT;
    if (safe > oc::note::clock::PPQN) {
        safe = static_cast<uint8_t>(oc::note::clock::PPQN);
    }
    const uint8_t ticks = static_cast<uint8_t>(
        oc::note::clock::PPQN / safe
    );
    return ticks == 0U ? 1U : ticks;
}

int32_t DrumPlaybackEngine::nudgeTickOffset_(
    int8_t nudge,
    uint8_t ticksPerStep
) {
    const int32_t clamped = std::max<int32_t>(-50, std::min<int32_t>(50, nudge));
    const int32_t scaled = clamped * ticksPerStep;
    return scaled >= 0
        ? (scaled + 50) / 100
        : -(((-scaled) + 50) / 100);
}

FLASHMEM uint32_t DrumPlaybackEngine::probabilityHash_(
    uint32_t runSeed,
    uint8_t lane,
    uint32_t cycleIndex,
    uint8_t step
) {
    uint32_t value = runSeed * 747796405U;
    value ^= static_cast<uint32_t>(lane) * 2246822519U;
    value ^= cycleIndex * 2891336453U;
    value ^= static_cast<uint32_t>(step) * 277803737U;
    value ^= value >> 16U;
    value *= 2246822519U;
    value ^= value >> 13U;
    value *= 3266489917U;
    value ^= value >> 16U;
    return value;
}

}  // namespace core::sequencer
