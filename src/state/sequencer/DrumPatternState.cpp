#include "state/sequencer/DrumPatternState.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
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

FLASHMEM void bump(uint32_t& revision) {
    ++revision;
    if (revision == 0U) revision = 1U;
}

FLASHMEM std::array<char, DRUM_LANE_NAME_MAX_LENGTH + 1U> canonicalName(
    const std::array<char, DRUM_LANE_NAME_MAX_LENGTH + 1U>& source
) {
    std::array<char, DRUM_LANE_NAME_MAX_LENGTH + 1U> result{};
    for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
        const unsigned char value = static_cast<unsigned char>(
            source[i]
        );
        if (value == 0U) break;
        if (!std::isprint(value)) break;
        result[i] = static_cast<char>(value);
    }
    return result;
}

FLASHMEM bool descriptorsEqual(
    const DrumLaneDescriptor& lhs,
    const DrumLaneDescriptor& rhs
) {
    return lhs.midiNote == rhs.midiNote && lhs.role == rhs.role &&
           lhs.overrideMask == rhs.overrideMask &&
           lhs.icon == rhs.icon && lhs.colorIndex == rhs.colorIndex &&
           lhs.name == rhs.name;
}

FLASHMEM DrumLaneDescriptor descriptor(
    uint8_t midiNote,
    DrumLaneRole role
) {
    return DrumLaneDescriptor{
        .midiNote = midiNote,
        .role = role,
    };
}

FLASHMEM void swapLanePatterns(DrumLanePattern& lhs, DrumLanePattern& rhs) {
    std::swap(lhs.timing, rhs.timing);
    std::swap(lhs.enabledMask, rhs.enabledMask);
    for (uint8_t step = 0U; step < DRUM_MAX_STEPS; ++step) {
        std::swap(lhs.velocity[step], rhs.velocity[step]);
        std::swap(lhs.gate[step], rhs.gate[step]);
        std::swap(lhs.nudge[step], rhs.nudge[step]);
        std::swap(lhs.probability[step], rhs.probability[step]);
    }
}

constexpr uint16_t advancedStepKey(uint8_t lane, uint8_t step) {
    return lane < DRUM_MAX_LANES && step < DRUM_MAX_STEPS
        ? static_cast<uint16_t>(
              (static_cast<uint16_t>(lane) << 7U) | step)
        : DRUM_ADVANCED_STEP_KEY_INVALID;
}

constexpr uint8_t advancedStepKeyLane(uint16_t key) {
    return static_cast<uint8_t>((key >> 7U) & 0x0FU);
}

constexpr uint8_t advancedStepKeyStep(uint16_t key) {
    return static_cast<uint8_t>(key & 0x7FU);
}

FLASHMEM void remapAdvancedLaneInsert(
    std::array<uint16_t, DRUM_ADVANCED_ROOT_SLOT_COUNT>& keys,
    uint8_t index,
    uint8_t oldCount
) {
    for (auto& key : keys) {
        if (key == DRUM_ADVANCED_STEP_KEY_INVALID) continue;
        const uint8_t lane = advancedStepKeyLane(key);
        if (lane >= oldCount) {
            key = DRUM_ADVANCED_STEP_KEY_INVALID;
        } else if (lane >= index) {
            key = advancedStepKey(static_cast<uint8_t>(lane + 1U),
                                  advancedStepKeyStep(key));
        }
    }
}

FLASHMEM void remapAdvancedLaneRemove(
    std::array<uint16_t, DRUM_ADVANCED_ROOT_SLOT_COUNT>& keys,
    uint8_t index,
    uint8_t oldCount
) {
    for (auto& key : keys) {
        if (key == DRUM_ADVANCED_STEP_KEY_INVALID) continue;
        const uint8_t lane = advancedStepKeyLane(key);
        if (lane >= oldCount || lane == index) {
            key = DRUM_ADVANCED_STEP_KEY_INVALID;
        } else if (lane > index) {
            key = advancedStepKey(static_cast<uint8_t>(lane - 1U),
                                  advancedStepKeyStep(key));
        }
    }
}

FLASHMEM void remapAdvancedLaneMove(
    std::array<uint16_t, DRUM_ADVANCED_ROOT_SLOT_COUNT>& keys,
    uint8_t from,
    uint8_t to
) {
    for (auto& key : keys) {
        if (key == DRUM_ADVANCED_STEP_KEY_INVALID) continue;
        uint8_t lane = advancedStepKeyLane(key);
        if (lane == from) {
            lane = to;
        } else if (from < to && lane > from && lane <= to) {
            --lane;
        } else if (from > to && lane >= to && lane < from) {
            ++lane;
        }
        key = advancedStepKey(lane, advancedStepKeyStep(key));
    }
}

}  // namespace

FLASHMEM DrumLaneRolePreset drumLaneRolePreset(DrumLaneRole role) {
    switch (role) {
        case DrumLaneRole::KICK:
            return {"Kick", DrumLaneIcon::KICK, 0U};
        case DrumLaneRole::SNARE:
            return {"Snare", DrumLaneIcon::SNARE, 3U};
        case DrumLaneRole::CLOSED_HAT:
            return {"C.Hat", DrumLaneIcon::CLOSED_HAT, 4U};
        case DrumLaneRole::OPEN_HAT:
            return {"O.Hat", DrumLaneIcon::OPEN_HAT, 5U};
        case DrumLaneRole::CLAP:
            return {"Clap", DrumLaneIcon::CLAP, 7U};
        case DrumLaneRole::LOW_TOM:
            return {"Tom L", DrumLaneIcon::TOM, 2U};
        case DrumLaneRole::HIGH_TOM:
            return {"Tom H", DrumLaneIcon::TOM, 3U};
        case DrumLaneRole::PERCUSSION:
            return {"Perc", DrumLaneIcon::PERCUSSION, 6U};
        case DrumLaneRole::CUSTOM:
        default:
            return {"Lane", DrumLaneIcon::GENERIC, 0U};
    }
}

FLASHMEM DrumLaneDescriptor canonicalDrumLaneDescriptor(
    DrumLaneDescriptor descriptorValue
) {
    descriptorValue.midiNote = clampMidi7(descriptorValue.midiNote);
    if (descriptorValue.role > DrumLaneRole::PERCUSSION) {
        descriptorValue.role = DrumLaneRole::CUSTOM;
    }
    descriptorValue.overrideMask = static_cast<uint8_t>(
        descriptorValue.overrideMask & DRUM_LANE_OVERRIDE_ALL
    );
    const auto preset = drumLaneRolePreset(descriptorValue.role);

    if ((descriptorValue.overrideMask & DRUM_LANE_OVERRIDE_ICON) == 0U ||
        descriptorValue.icon >= DrumLaneIcon::COUNT ||
        descriptorValue.icon == preset.icon) {
        descriptorValue.overrideMask = static_cast<uint8_t>(
            descriptorValue.overrideMask & ~DRUM_LANE_OVERRIDE_ICON
        );
        descriptorValue.icon = DrumLaneIcon::GENERIC;
    }

    if ((descriptorValue.overrideMask & DRUM_LANE_OVERRIDE_COLOR) == 0U) {
        descriptorValue.colorIndex = 0U;
    } else {
        descriptorValue.colorIndex = static_cast<uint8_t>(
            descriptorValue.colorIndex % DRUM_LANE_COLOR_COUNT
        );
        if (descriptorValue.colorIndex == preset.colorIndex) {
            descriptorValue.overrideMask = static_cast<uint8_t>(
                descriptorValue.overrideMask & ~DRUM_LANE_OVERRIDE_COLOR
            );
            descriptorValue.colorIndex = 0U;
        }
    }

    descriptorValue.name = canonicalName(descriptorValue.name);
    if ((descriptorValue.overrideMask & DRUM_LANE_OVERRIDE_NAME) == 0U ||
        descriptorValue.name[0] == '\0' ||
        std::strcmp(descriptorValue.name.data(), preset.name) == 0) {
        descriptorValue.overrideMask = static_cast<uint8_t>(
            descriptorValue.overrideMask & ~DRUM_LANE_OVERRIDE_NAME
        );
        descriptorValue.name.fill('\0');
    }
    return descriptorValue;
}

FLASHMEM const char* drumLaneRoleLabel(DrumLaneRole role) {
    return drumLaneRolePreset(role).name;
}

FLASHMEM const char* drumKitPresetLabel(DrumKitPreset preset) {
    switch (preset) {
        case DrumKitPreset::GENERAL_MIDI: return "General MIDI";
        case DrumKitPreset::EMPTY:
        default: return "New kit";
    }
}

FLASHMEM DrumLaneIcon drumLaneDefaultIcon(DrumLaneRole role) {
    return drumLaneRolePreset(role).icon;
}

FLASHMEM uint8_t drumLaneDefaultColorIndex(DrumLaneRole role) {
    return drumLaneRolePreset(role).colorIndex;
}

FLASHMEM const char* drumLaneIconLabel(DrumLaneIcon icon) {
    switch (icon) {
        case DrumLaneIcon::KICK: return "Kick";
        case DrumLaneIcon::SNARE: return "Snare";
        case DrumLaneIcon::CLOSED_HAT: return "C.Hat";
        case DrumLaneIcon::OPEN_HAT: return "O.Hat";
        case DrumLaneIcon::CLAP: return "Clap";
        case DrumLaneIcon::TOM: return "Tom";
        case DrumLaneIcon::PERCUSSION: return "Perc";
        case DrumLaneIcon::GENERIC:
        case DrumLaneIcon::COUNT:
        default: return "Generic";
    }
}

FLASHMEM const char* drumLaneDisplayName(
    const DrumLaneDescriptor& descriptor
) {
    return (descriptor.overrideMask & DRUM_LANE_OVERRIDE_NAME) != 0U &&
            descriptor.name[0] != '\0'
        ? descriptor.name.data()
        : drumLaneRoleLabel(descriptor.role);
}

FLASHMEM DrumLaneIcon drumLaneDisplayIcon(
    const DrumLaneDescriptor& descriptor
) {
    return (descriptor.overrideMask & DRUM_LANE_OVERRIDE_ICON) != 0U
        ? descriptor.icon
        : drumLaneDefaultIcon(descriptor.role);
}

FLASHMEM uint8_t drumLaneDisplayColorIndex(
    const DrumLaneDescriptor& descriptor
) {
    return (descriptor.overrideMask & DRUM_LANE_OVERRIDE_COLOR) != 0U
        ? static_cast<uint8_t>(descriptor.colorIndex % DRUM_LANE_COLOR_COUNT)
        : drumLaneDefaultColorIndex(descriptor.role);
}

FLASHMEM bool setDrumLaneRole(
    DrumLaneDescriptor& descriptorValue,
    DrumLaneRole role
) {
    const auto previous = descriptorValue;
    descriptorValue.role = role;
    descriptorValue = canonicalDrumLaneDescriptor(descriptorValue);
    return !descriptorsEqual(previous, descriptorValue);
}

FLASHMEM bool setDrumLaneName(
    DrumLaneDescriptor& descriptorValue,
    const char* name
) {
    const auto previous = descriptorValue;
    auto next = descriptorValue;
    next.name.fill('\0');
    if (name != nullptr) {
        for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
            const unsigned char value = static_cast<unsigned char>(name[i]);
            if (value == 0U || !std::isprint(value)) break;
            next.name[i] = static_cast<char>(value);
        }
    }
    next.overrideMask = static_cast<uint8_t>(
        next.overrideMask | DRUM_LANE_OVERRIDE_NAME
    );
    descriptorValue = canonicalDrumLaneDescriptor(next);
    return !descriptorsEqual(previous, descriptorValue);
}

FLASHMEM bool setDrumLaneIcon(
    DrumLaneDescriptor& descriptorValue,
    DrumLaneIcon icon
) {
    const auto previous = descriptorValue;
    descriptorValue.icon = icon;
    descriptorValue.overrideMask = static_cast<uint8_t>(
        descriptorValue.overrideMask | DRUM_LANE_OVERRIDE_ICON
    );
    descriptorValue = canonicalDrumLaneDescriptor(descriptorValue);
    return !descriptorsEqual(previous, descriptorValue);
}

FLASHMEM bool setDrumLaneColorIndex(
    DrumLaneDescriptor& descriptorValue,
    uint8_t colorIndex
) {
    const auto previous = descriptorValue;
    descriptorValue.colorIndex = colorIndex;
    descriptorValue.overrideMask = static_cast<uint8_t>(
        descriptorValue.overrideMask | DRUM_LANE_OVERRIDE_COLOR
    );
    descriptorValue = canonicalDrumLaneDescriptor(descriptorValue);
    return !descriptorsEqual(previous, descriptorValue);
}

FLASHMEM void DrumKitState::resetEmpty() {
    laneCount = 0U;
    lanes.fill({});
    bump(revision);
}

FLASHMEM void DrumKitState::resetGeneralMidi() {
    laneCount = DRUM_DEFAULT_LANE_COUNT;
    lanes.fill({});
    const std::array<DrumLaneDescriptor, DRUM_DEFAULT_LANE_COUNT> defaults = {{
        descriptor(36U, DrumLaneRole::KICK),
        descriptor(38U, DrumLaneRole::SNARE),
        descriptor(42U, DrumLaneRole::CLOSED_HAT),
        descriptor(46U, DrumLaneRole::OPEN_HAT),
        descriptor(39U, DrumLaneRole::CLAP),
        descriptor(45U, DrumLaneRole::LOW_TOM),
        descriptor(50U, DrumLaneRole::HIGH_TOM),
        descriptor(56U, DrumLaneRole::PERCUSSION),
    }};
    std::copy(defaults.begin(), defaults.end(), lanes.begin());
    bump(revision);
}

FLASHMEM void DrumKitState::applyPreset(DrumKitPreset preset) {
    switch (preset) {
        case DrumKitPreset::GENERAL_MIDI:
            resetGeneralMidi();
            return;
        case DrumKitPreset::EMPTY:
        default:
            resetEmpty();
            return;
    }
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
    descriptor = canonicalDrumLaneDescriptor(descriptor);
    if (descriptorsEqual(lanes[lane], descriptor)) return false;
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

FLASHMEM bool sameDrumLanePattern(
    const DrumLanePattern& lhs,
    const DrumLanePattern& rhs
) {
    return lhs.timing.mode == rhs.timing.mode &&
        lhs.timing.length == rhs.timing.length &&
        lhs.timing.stepsPerBeat == rhs.timing.stepsPerBeat &&
        lhs.enabledMask == rhs.enabledMask &&
        lhs.velocity == rhs.velocity &&
        lhs.gate == rhs.gate &&
        lhs.nudge == rhs.nudge &&
        lhs.probability == rhs.probability;
}

FLASHMEM void DrumPatternState::reset() {
    defaultLength = DRUM_DEFAULT_LENGTH;
    defaultStepsPerBeat = DRUM_DEFAULT_STEPS_PER_BEAT;
    for (auto& lane : lanes) lane.reset();
    bump(revision);
}

FLASHMEM uint8_t DrumPatternState::effectiveLength(uint8_t lane) const {
    if (lane >= DRUM_MAX_LANES) return 0U;
    const auto& timing = lanes[lane].timing;
    return timing.mode == DrumLaneTimingMode::CUSTOM
        ? clampLength(timing.length)
        : clampLength(defaultLength);
}

FLASHMEM uint8_t DrumPatternState::effectiveStepsPerBeat(uint8_t lane) const {
    if (lane >= DRUM_MAX_LANES) return 0U;
    const auto& timing = lanes[lane].timing;
    return timing.mode == DrumLaneTimingMode::CUSTOM
        ? clampStepsPerBeat(timing.stepsPerBeat)
        : clampStepsPerBeat(defaultStepsPerBeat);
}

FLASHMEM bool DrumPatternState::stepEnabled(uint8_t lane, uint8_t step) const {
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

FLASHMEM bool DrumPatternState::replaceLanePattern(
    uint8_t lane,
    const DrumLanePattern& source
) {
    if (lane >= DRUM_MAX_LANES ||
        sameDrumLanePattern(lanes[lane], source)) {
        return false;
    }
    lanes[lane] = source;
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

FLASHMEM void DrumTrackState::reset(DrumKitPreset preset) {
    kit.applyPreset(preset);
    pattern.reset();
    advancedStepKeys.fill(DRUM_ADVANCED_STEP_KEY_INVALID);
}

FLASHMEM bool DrumTrackState::insertLane(
    uint8_t index,
    DrumLaneDescriptor descriptorValue
) {
    const uint8_t count = clampLaneCount(kit.laneCount);
    if (count >= DRUM_MAX_LANES || index > count) return false;

    descriptorValue = canonicalDrumLaneDescriptor(descriptorValue);
    for (uint8_t lane = count; lane > index; --lane) {
        kit.lanes[lane] = kit.lanes[lane - 1U];
        pattern.lanes[lane] = pattern.lanes[lane - 1U];
    }
    kit.lanes[index] = descriptorValue;
    pattern.lanes[index].reset();
    remapAdvancedLaneInsert(advancedStepKeys, index, count);
    kit.laneCount = static_cast<uint8_t>(count + 1U);
    bump(kit.revision);
    bump(pattern.revision);
    return true;
}

FLASHMEM bool DrumTrackState::appendLane(DrumLaneDescriptor descriptorValue) {
    return insertLane(kit.laneCount, descriptorValue);
}

FLASHMEM bool DrumTrackState::removeLane(uint8_t index) {
    const uint8_t count = clampLaneCount(kit.laneCount);
    if (index >= count) return false;
    for (uint8_t lane = index; lane + 1U < count; ++lane) {
        kit.lanes[lane] = kit.lanes[lane + 1U];
        pattern.lanes[lane] = pattern.lanes[lane + 1U];
    }
    kit.lanes[count - 1U] = {};
    pattern.lanes[count - 1U].reset();
    remapAdvancedLaneRemove(advancedStepKeys, index, count);
    kit.laneCount = static_cast<uint8_t>(count - 1U);
    bump(kit.revision);
    bump(pattern.revision);
    return true;
}

FLASHMEM bool DrumTrackState::moveLane(uint8_t from, uint8_t to) {
    const uint8_t count = clampLaneCount(kit.laneCount);
    if (from >= count || to >= count || from == to) return false;
    const int direction = from < to ? 1 : -1;
    for (int lane = from; lane != static_cast<int>(to); lane += direction) {
        const uint8_t current = static_cast<uint8_t>(lane);
        const uint8_t adjacent = static_cast<uint8_t>(lane + direction);
        std::swap(kit.lanes[current], kit.lanes[adjacent]);
        swapLanePatterns(pattern.lanes[current], pattern.lanes[adjacent]);
    }
    remapAdvancedLaneMove(advancedStepKeys, from, to);
    bump(kit.revision);
    bump(pattern.revision);
    return true;
}

FLASHMEM int16_t DrumTrackState::advancedRootSlot(
    uint8_t lane,
    uint8_t step
) const {
    const uint16_t key = advancedStepKey(lane, step);
    if (key == DRUM_ADVANCED_STEP_KEY_INVALID) return -1;
    for (uint8_t slot = 0U; slot < advancedStepKeys.size(); ++slot) {
        if (advancedStepKeys[slot] == key) return slot;
    }
    return -1;
}

FLASHMEM int16_t DrumTrackState::firstFreeAdvancedRootSlot() const {
    for (uint8_t slot = 0U; slot < advancedStepKeys.size(); ++slot) {
        if (advancedStepKeys[slot] == DRUM_ADVANCED_STEP_KEY_INVALID) {
            return slot;
        }
    }
    return -1;
}

FLASHMEM bool DrumTrackState::bindAdvancedRootSlot(
    uint8_t slot,
    uint8_t lane,
    uint8_t step
) {
    if (slot >= advancedStepKeys.size()) return false;
    const uint16_t key = advancedStepKey(lane, step);
    if (key == DRUM_ADVANCED_STEP_KEY_INVALID) return false;
    const int16_t existing = advancedRootSlot(lane, step);
    if (existing >= 0) return existing == slot;
    if (advancedStepKeys[slot] != DRUM_ADVANCED_STEP_KEY_INVALID) return false;
    advancedStepKeys[slot] = key;
    bump(pattern.revision);
    return true;
}

FLASHMEM bool DrumTrackState::releaseAdvancedRootSlot(
    uint8_t lane,
    uint8_t step
) {
    const int16_t slot = advancedRootSlot(lane, step);
    if (slot < 0) return false;
    advancedStepKeys[static_cast<uint8_t>(slot)] =
        DRUM_ADVANCED_STEP_KEY_INVALID;
    bump(pattern.revision);
    return true;
}

FLASHMEM int16_t ensureDrumAdvancedRootSlot(
    DrumTrackState& drumTrack,
    const SequencerPatternState& pattern,
    uint8_t lane,
    uint8_t step,
    bool& mappingChanged
) {
    mappingChanged = false;
    if (lane >= drumTrack.kit.laneCount || lane >= DRUM_MAX_LANES ||
        step >= drumTrack.pattern.effectiveLength(lane) ||
        step >= DRUM_MAX_STEPS) {
        return -1;
    }

    const int16_t existing = drumTrack.advancedRootSlot(lane, step);
    if (existing >= 0) return existing;

    const auto* graph = graphView(pattern);
    for (uint8_t slot = 0U; slot < DRUM_ADVANCED_ROOT_SLOT_COUNT; ++slot) {
        if (drumTrack.advancedStepKeys[slot] !=
            DRUM_ADVANCED_STEP_KEY_INVALID) {
            continue;
        }
        const auto nodeId = rootStepNodeId(slot);
        const auto* node = graph == nullptr ? nullptr : graph->stepNode(nodeId);
        if (node != nullptr && !isDefaultSequencerGraphNodePayload(*node)) {
            continue;
        }
        if (!drumTrack.bindAdvancedRootSlot(slot, lane, step)) return -1;
        mappingChanged = true;
        return slot;
    }
    return -1;
}

FLASHMEM int16_t DrumPatternRuntimeSnapshot::advancedRootSlot(
    uint8_t lane,
    uint8_t step
) const {
    const uint16_t key = advancedStepKey(lane, step);
    if (key == DRUM_ADVANCED_STEP_KEY_INVALID) return -1;
    for (uint8_t slot = 0U; slot < advancedStepKeys.size(); ++slot) {
        if (advancedStepKeys[slot] == key) return slot;
    }
    return -1;
}

FLASHMEM uint32_t drumRuntimeRevision(const DrumTrackState& source) {
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
    out.advancedStepKeys = source.advancedStepKeys;
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
