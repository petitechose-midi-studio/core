#include "state/sequencer/DrumPatternState.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>

namespace core::state::sequencer {

namespace {

constexpr uint8_t clampLaneCount(uint8_t count) {
    return count > DRUM_MAX_LANES ? DRUM_MAX_LANES : count;
}

constexpr uint8_t clampLength(uint8_t length) {
    if (length == 0U) return 1U;
    return length > DRUM_MAX_STEPS ? DRUM_MAX_STEPS : length;
}

constexpr uint8_t clampStepsPerBeat(uint8_t stepsPerBeat) {
    if (stepsPerBeat == 0U) return 1U;
    return stepsPerBeat > oc::note::clock::PPQN
        ? static_cast<uint8_t>(oc::note::clock::PPQN)
        : stepsPerBeat;
}

constexpr uint8_t clampMidi7(uint8_t value) {
    return value > 127U ? 127U : value;
}

constexpr uint16_t clampGate(uint16_t value) {
    return value > DRUM_MAX_GATE_PERCENT ? DRUM_MAX_GATE_PERCENT : value;
}

constexpr int8_t clampNudge(int value) {
    if (value < -50) return -50;
    if (value > 50) return 50;
    return static_cast<int8_t>(value);
}

constexpr uint8_t clampProbability(uint8_t value) {
    return value > 100U ? 100U : value;
}

void bump(uint32_t& revision) {
    ++revision;
    if (revision == 0U) revision = 1U;
}

}  // namespace

FLASHMEM const char* drumLaneRoleLabel(DrumLaneRole role) {
    switch (role) {
        case DrumLaneRole::KICK: return "Kick";
        case DrumLaneRole::SNARE: return "Snare";
        case DrumLaneRole::CLOSED_HAT: return "C.Hat";
        case DrumLaneRole::OPEN_HAT: return "O.Hat";
        case DrumLaneRole::CLAP: return "Clap";
        case DrumLaneRole::LOW_TOM: return "Tom L";
        case DrumLaneRole::HIGH_TOM: return "Tom H";
        case DrumLaneRole::PERCUSSION: return "Perc";
        case DrumLaneRole::CUSTOM:
        default: return "Lane";
    }
}

FLASHMEM void DrumKitState::resetGeneralMidi() {
    laneCount = DRUM_DEFAULT_LANE_COUNT;
    lanes.fill({});
    constexpr std::array<DrumLaneDescriptor, DRUM_DEFAULT_LANE_COUNT> defaults = {{
        {36U, DrumLaneRole::KICK},
        {38U, DrumLaneRole::SNARE},
        {42U, DrumLaneRole::CLOSED_HAT},
        {46U, DrumLaneRole::OPEN_HAT},
        {39U, DrumLaneRole::CLAP},
        {45U, DrumLaneRole::LOW_TOM},
        {50U, DrumLaneRole::HIGH_TOM},
        {56U, DrumLaneRole::PERCUSSION},
    }};
    std::copy(defaults.begin(), defaults.end(), lanes.begin());
    bump(revision);
}

FLASHMEM bool DrumKitState::setLaneCount(uint8_t count) {
    const uint8_t clamped = clampLaneCount(count);
    if (laneCount == clamped) return false;
    laneCount = clamped;
    bump(revision);
    return true;
}

FLASHMEM bool DrumKitState::setLane(
    uint8_t lane,
    DrumLaneDescriptor descriptor
) {
    if (lane >= DRUM_MAX_LANES) return false;
    descriptor.midiNote = clampMidi7(descriptor.midiNote);
    if (lanes[lane].midiNote == descriptor.midiNote &&
        lanes[lane].role == descriptor.role) {
        return false;
    }
    lanes[lane] = descriptor;
    bump(revision);
    return true;
}

FLASHMEM void DrumLanePattern::reset() {
    timing = {};
    enabledMask = {};
    velocity.fill(DRUM_DEFAULT_VELOCITY);
    gate.fill(DRUM_DEFAULT_GATE_PERCENT);
    nudge.fill(0);
    probability.fill(DRUM_DEFAULT_PROBABILITY);
}

FLASHMEM void DrumPatternState::reset() {
    defaultLength = DRUM_DEFAULT_LENGTH;
    defaultStepsPerBeat = DRUM_DEFAULT_STEPS_PER_BEAT;
    for (auto& lane : lanes) lane.reset();
    bump(revision);
}

uint8_t DrumPatternState::effectiveLength(uint8_t lane) const {
    if (lane >= DRUM_MAX_LANES) return 0U;
    const auto& timing = lanes[lane].timing;
    return timing.mode == DrumLaneTimingMode::CUSTOM
        ? clampLength(timing.length)
        : clampLength(defaultLength);
}

uint8_t DrumPatternState::effectiveStepsPerBeat(uint8_t lane) const {
    if (lane >= DRUM_MAX_LANES) return 0U;
    const auto& timing = lanes[lane].timing;
    return timing.mode == DrumLaneTimingMode::CUSTOM
        ? clampStepsPerBeat(timing.stepsPerBeat)
        : clampStepsPerBeat(defaultStepsPerBeat);
}

bool DrumPatternState::stepEnabled(uint8_t lane, uint8_t step) const {
    return lane < DRUM_MAX_LANES && step < effectiveLength(lane) &&
           lanes[lane].enabledMask.test(step);
}

FLASHMEM bool DrumPatternState::setDefaults(
    uint8_t length,
    uint8_t stepsPerBeat
) {
    const uint8_t nextLength = clampLength(length);
    const uint8_t nextStepsPerBeat = clampStepsPerBeat(stepsPerBeat);
    if (defaultLength == nextLength &&
        defaultStepsPerBeat == nextStepsPerBeat) {
        return false;
    }
    defaultLength = nextLength;
    defaultStepsPerBeat = nextStepsPerBeat;
    bump(revision);
    return true;
}

FLASHMEM bool DrumPatternState::setLaneTimingInherited(uint8_t lane) {
    if (lane >= DRUM_MAX_LANES ||
        lanes[lane].timing.mode == DrumLaneTimingMode::INHERIT_PATTERN) {
        return false;
    }
    lanes[lane].timing.mode = DrumLaneTimingMode::INHERIT_PATTERN;
    bump(revision);
    return true;
}

FLASHMEM bool DrumPatternState::setLaneTimingCustom(
    uint8_t lane,
    uint8_t length,
    uint8_t stepsPerBeat
) {
    if (lane >= DRUM_MAX_LANES) return false;
    DrumLaneTiming next{
        DrumLaneTimingMode::CUSTOM,
        clampLength(length),
        clampStepsPerBeat(stepsPerBeat),
    };
    auto& current = lanes[lane].timing;
    if (current.mode == next.mode && current.length == next.length &&
        current.stepsPerBeat == next.stepsPerBeat) {
        return false;
    }
    current = next;
    bump(revision);
    return true;
}

FLASHMEM bool DrumPatternState::setStepEnabled(
    uint8_t lane,
    uint8_t step,
    bool enabled
) {
    if (lane >= DRUM_MAX_LANES || step >= DRUM_MAX_STEPS) return false;
    auto& mask = lanes[lane].enabledMask;
    if (mask.test(step) == enabled) return false;
    mask.setBit(step, enabled);
    bump(revision);
    return true;
}

FLASHMEM bool DrumPatternState::toggleStep(uint8_t lane, uint8_t step) {
    if (lane >= DRUM_MAX_LANES || step >= DRUM_MAX_STEPS) return false;
    return setStepEnabled(lane, step, !lanes[lane].enabledMask.test(step));
}

FLASHMEM bool DrumPatternState::setStepVelocity(
    uint8_t lane,
    uint8_t step,
    uint8_t velocityValue
) {
    if (lane >= DRUM_MAX_LANES || step >= DRUM_MAX_STEPS) return false;
    const uint8_t value = clampMidi7(velocityValue);
    if (lanes[lane].velocity[step] == value) return false;
    lanes[lane].velocity[step] = value;
    bump(revision);
    return true;
}

FLASHMEM bool DrumPatternState::setStepGate(
    uint8_t lane,
    uint8_t step,
    uint16_t gatePercent
) {
    if (lane >= DRUM_MAX_LANES || step >= DRUM_MAX_STEPS) return false;
    const uint16_t value = clampGate(gatePercent);
    if (lanes[lane].gate[step] == value) return false;
    lanes[lane].gate[step] = value;
    bump(revision);
    return true;
}

FLASHMEM bool DrumPatternState::setStepNudge(
    uint8_t lane,
    uint8_t step,
    int8_t nudgePercent
) {
    if (lane >= DRUM_MAX_LANES || step >= DRUM_MAX_STEPS) return false;
    const int8_t value = clampNudge(nudgePercent);
    if (lanes[lane].nudge[step] == value) return false;
    lanes[lane].nudge[step] = value;
    bump(revision);
    return true;
}

FLASHMEM bool DrumPatternState::setStepProbability(
    uint8_t lane,
    uint8_t step,
    uint8_t probabilityValue
) {
    if (lane >= DRUM_MAX_LANES || step >= DRUM_MAX_STEPS) return false;
    const uint8_t value = clampProbability(probabilityValue);
    if (lanes[lane].probability[step] == value) return false;
    lanes[lane].probability[step] = value;
    bump(revision);
    return true;
}

FLASHMEM void DrumTrackState::reset() {
    kit.resetGeneralMidi();
    pattern.reset();
}

uint32_t drumRuntimeRevision(const DrumTrackState& source) {
    return source.kit.revision ^
        (source.pattern.revision + 0x9E3779B9U +
         (source.kit.revision << 6U) + (source.kit.revision >> 2U));
}

FLASHMEM void captureDrumRuntimeSnapshot(
    const DrumTrackState& source,
    DrumPatternRuntimeSnapshot& out
) {
    out.laneCount = clampLaneCount(source.kit.laneCount);
    out.revision = drumRuntimeRevision(source);
    for (uint8_t lane = 0U; lane < DRUM_MAX_LANES; ++lane) {
        const auto& authored = source.pattern.lanes[lane];
        auto& runtime = out.lanes[lane];
        runtime.midiNote = clampMidi7(source.kit.lanes[lane].midiNote);
        runtime.length = source.pattern.effectiveLength(lane);
        runtime.stepsPerBeat = source.pattern.effectiveStepsPerBeat(lane);
        runtime.enabledMask = authored.enabledMask;
        runtime.velocity = authored.velocity;
        runtime.gate = authored.gate;
        runtime.nudge = authored.nudge;
        runtime.probability = authored.probability;
    }
}

}  // namespace core::state::sequencer
