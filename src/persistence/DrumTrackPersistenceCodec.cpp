#include "persistence/DrumTrackPersistenceCodec.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>

namespace core::persistence::sequencer_codec {

namespace {

using core::state::sequencer::DRUM_MAX_GATE_PERCENT;
using core::state::sequencer::DRUM_MAX_LANES;
using core::state::sequencer::DRUM_MAX_STEPS;
using core::state::sequencer::DRUM_LANE_OVERRIDE_ALL;
using core::state::sequencer::DRUM_LANE_OVERRIDE_COLOR;
using core::state::sequencer::DRUM_LANE_OVERRIDE_ICON;
using core::state::sequencer::DRUM_LANE_OVERRIDE_NAME;
using core::state::sequencer::DRUM_LANE_COLOR_COUNT;
using core::state::sequencer::DRUM_LANE_NAME_MAX_LENGTH;
using core::state::sequencer::DRUM_ADVANCED_ROOT_SLOT_COUNT;
using core::state::sequencer::DRUM_ADVANCED_STEP_KEY_INVALID;
using core::state::sequencer::DrumLaneIcon;
using core::state::sequencer::DrumLaneRole;
using core::state::sequencer::DrumLaneTimingMode;
using core::state::sequencer::DrumTrackState;

constexpr uint16_t HEADER_SIZE = 12U;
constexpr uint16_t DESCRIPTOR_RECORD_SIZE =
    5U + DRUM_LANE_NAME_MAX_LENGTH;
constexpr uint16_t DESCRIPTORS_SIZE =
    DESCRIPTOR_RECORD_SIZE * DRUM_MAX_LANES;
constexpr uint16_t LANE_RECORD_SIZE = 660U;
constexpr uint16_t ADVANCED_INDEX_SIZE =
    2U * DRUM_ADVANCED_ROOT_SLOT_COUNT;

static_assert(
    HEADER_SIZE + DESCRIPTORS_SIZE +
        DRUM_MAX_LANES * LANE_RECORD_SIZE + ADVANCED_INDEX_SIZE ==
            DRUM_TRACK_RECORD_SIZE
);

FLASHMEM void writeU16(uint8_t* data, uint16_t& offset, uint16_t value) {
    data[offset++] = static_cast<uint8_t>(value & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

FLASHMEM void writeU32(uint8_t* data, uint16_t& offset, uint32_t value) {
    data[offset++] = static_cast<uint8_t>(value & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

FLASHMEM uint16_t readU16(const uint8_t* data, uint16_t& offset) {
    const uint16_t value = static_cast<uint16_t>(
        static_cast<uint16_t>(data[offset]) |
        static_cast<uint16_t>(
            static_cast<uint16_t>(data[offset + 1U]) << 8U));
    offset = static_cast<uint16_t>(offset + 2U);
    return value;
}

FLASHMEM uint32_t readU32(const uint8_t* data, uint16_t& offset) {
    const uint32_t value = static_cast<uint32_t>(data[offset]) |
        (static_cast<uint32_t>(data[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(data[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(data[offset + 3U]) << 24U);
    offset = static_cast<uint16_t>(offset + 4U);
    return value;
}

constexpr bool validLength(uint8_t value) {
    return value >= 1U && value <= DRUM_MAX_STEPS;
}

constexpr bool validStepsPerBeat(uint8_t value) {
    return value >= 1U && value <= oc::note::clock::PPQN;
}

constexpr bool validRole(DrumLaneRole value) {
    return value >= DrumLaneRole::CUSTOM &&
           value <= DrumLaneRole::PERCUSSION;
}

constexpr bool validIcon(DrumLaneIcon value) {
    return value >= DrumLaneIcon::GENERIC && value < DrumLaneIcon::COUNT;
}

FLASHMEM bool validName(
    const std::array<char, DRUM_LANE_NAME_MAX_LENGTH + 1U>& name
) {
    if (name[DRUM_LANE_NAME_MAX_LENGTH] != '\0') return false;
    bool terminated = false;
    for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
        const uint8_t value = static_cast<uint8_t>(name[i]);
        if (value == 0U) {
            terminated = true;
            continue;
        }
        if (terminated || value < 32U || value > 126U) return false;
    }
    return true;
}

FLASHMEM bool validEncodedName(const uint8_t* data) {
    bool terminated = false;
    for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
        const uint8_t value = data[i];
        if (value == 0U) {
            terminated = true;
            continue;
        }
        if (terminated || value < 32U || value > 126U) return false;
    }
    return true;
}

FLASHMEM bool encodedNameIsEmpty(const uint8_t* data) {
    for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
        if (data[i] != 0U) return false;
    }
    return true;
}

FLASHMEM bool encodedNameEquals(const uint8_t* data, const char* expected) {
    for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
        const uint8_t expectedValue = expected != nullptr && expected[i] != '\0'
            ? static_cast<uint8_t>(expected[i])
            : 0U;
        if (data[i] != expectedValue) return false;
        if (expectedValue == 0U) {
            for (uint8_t rest = i; rest < DRUM_LANE_NAME_MAX_LENGTH; ++rest) {
                if (data[rest] != 0U) return false;
            }
            return true;
        }
    }
    return expected == nullptr || expected[DRUM_LANE_NAME_MAX_LENGTH] == '\0';
}

constexpr bool validTimingMode(DrumLaneTimingMode value) {
    return value == DrumLaneTimingMode::INHERIT_PATTERN ||
           value == DrumLaneTimingMode::CUSTOM;
}

constexpr bool validAdvancedStepKey(uint16_t key, uint8_t laneCount) {
    if (key == DRUM_ADVANCED_STEP_KEY_INVALID) return true;
    const uint8_t lane = static_cast<uint8_t>((key >> 7U) & 0x0FU);
    const uint8_t step = static_cast<uint8_t>(key & 0x7FU);
    return lane < laneCount && lane < DRUM_MAX_LANES && step < DRUM_MAX_STEPS &&
           (key & 0xF800U) == 0U;
}

FLASHMEM bool advancedKeysAreCanonical(
    const std::array<uint16_t, DRUM_ADVANCED_ROOT_SLOT_COUNT>& keys,
    uint8_t laneCount
) {
    for (uint8_t i = 0U; i < keys.size(); ++i) {
        if (!validAdvancedStepKey(keys[i], laneCount)) return false;
        if (keys[i] == DRUM_ADVANCED_STEP_KEY_INVALID) continue;
        for (uint8_t j = static_cast<uint8_t>(i + 1U); j < keys.size(); ++j) {
            if (keys[j] == keys[i]) return false;
        }
    }
    return true;
}

FLASHMEM bool sourceIsCanonical(const DrumTrackState& source) {
    if (source.kit.laneCount > DRUM_MAX_LANES ||
        !validLength(source.pattern.defaultLength) ||
        !validStepsPerBeat(source.pattern.defaultStepsPerBeat)) {
        return false;
    }
    for (const auto& descriptor : source.kit.lanes) {
        const auto canonical =
            core::state::sequencer::canonicalDrumLaneDescriptor(descriptor);
        if (descriptor.midiNote > 127U || !validRole(descriptor.role) ||
            !validIcon(descriptor.icon) ||
            descriptor.colorIndex >= DRUM_LANE_COLOR_COUNT ||
            !validName(descriptor.name) ||
            descriptor.midiNote != canonical.midiNote ||
            descriptor.role != canonical.role ||
            descriptor.overrideMask != canonical.overrideMask ||
            descriptor.icon != canonical.icon ||
            descriptor.colorIndex != canonical.colorIndex ||
            descriptor.name != canonical.name) {
            return false;
        }
    }
    for (const auto& lane : source.pattern.lanes) {
        if (!validTimingMode(lane.timing.mode) ||
            !validLength(lane.timing.length) ||
            !validStepsPerBeat(lane.timing.stepsPerBeat)) {
            return false;
        }
        for (uint8_t step = 0U; step < DRUM_MAX_STEPS; ++step) {
            if (lane.velocity[step] > 127U ||
                lane.gate[step] > DRUM_MAX_GATE_PERCENT ||
                lane.nudge[step] < -50 || lane.nudge[step] > 50 ||
                lane.probability[step] > 100U) {
                return false;
            }
        }
    }
    return advancedKeysAreCanonical(
        source.advancedStepKeys,
        source.kit.laneCount
    );
}

/** Validation pass keeps decode atomic without a 10.6 KiB RAM1 temporary. */
FLASHMEM bool recordIsCanonical(const uint8_t* data, uint16_t size) {
    uint16_t offset = 0U;
    const uint8_t laneCount = data[offset++];
    const uint8_t defaultLength = data[offset++];
    const uint8_t defaultStepsPerBeat = data[offset++];
    const uint8_t reserved = data[offset++];
    if (laneCount > DRUM_MAX_LANES || !validLength(defaultLength) ||
        !validStepsPerBeat(defaultStepsPerBeat) || reserved != 0U) {
        return false;
    }
    offset = static_cast<uint16_t>(offset + 8U);  // authored revisions

    for (uint8_t lane = 0U; lane < DRUM_MAX_LANES; ++lane) {
        const uint8_t midiNote = data[offset++];
        const auto role = static_cast<DrumLaneRole>(data[offset++]);
        if (midiNote > 127U || !validRole(role)) return false;
        const uint8_t overrideMask = data[offset++];
        const auto icon = static_cast<DrumLaneIcon>(data[offset++]);
        const uint8_t colorIndex = data[offset++];
        const uint8_t* name = data + offset;
        const auto preset =
            core::state::sequencer::drumLaneRolePreset(role);
        if ((overrideMask & ~DRUM_LANE_OVERRIDE_ALL) != 0U ||
            !validIcon(icon) || colorIndex >= DRUM_LANE_COLOR_COUNT ||
            !validEncodedName(name) ||
            ((overrideMask & DRUM_LANE_OVERRIDE_ICON) == 0U &&
             icon != DrumLaneIcon::GENERIC) ||
            ((overrideMask & DRUM_LANE_OVERRIDE_ICON) != 0U &&
             icon == preset.icon) ||
            ((overrideMask & DRUM_LANE_OVERRIDE_COLOR) == 0U &&
             colorIndex != 0U) ||
            ((overrideMask & DRUM_LANE_OVERRIDE_COLOR) != 0U &&
             colorIndex == preset.colorIndex) ||
            ((overrideMask & DRUM_LANE_OVERRIDE_NAME) == 0U &&
             !encodedNameIsEmpty(name)) ||
            ((overrideMask & DRUM_LANE_OVERRIDE_NAME) != 0U &&
             (name[0] == 0U || encodedNameEquals(name, preset.name)))) {
            return false;
        }
        offset = static_cast<uint16_t>(
            offset + DRUM_LANE_NAME_MAX_LENGTH
        );
    }

    for (uint8_t lane = 0U; lane < DRUM_MAX_LANES; ++lane) {
        const auto mode = static_cast<DrumLaneTimingMode>(data[offset++]);
        const uint8_t length = data[offset++];
        const uint8_t stepsPerBeat = data[offset++];
        const uint8_t laneReserved = data[offset++];
        if (!validTimingMode(mode) || !validLength(length) ||
            !validStepsPerBeat(stepsPerBeat) || laneReserved != 0U) {
            return false;
        }
        offset = static_cast<uint16_t>(offset + 16U);  // enabled mask
        for (uint8_t step = 0U; step < DRUM_MAX_STEPS; ++step) {
            if (data[offset++] > 127U) return false;
        }
        for (uint8_t step = 0U; step < DRUM_MAX_STEPS; ++step) {
            if (readU16(data, offset) > DRUM_MAX_GATE_PERCENT) return false;
        }
        for (uint8_t step = 0U; step < DRUM_MAX_STEPS; ++step) {
            const int8_t nudge = static_cast<int8_t>(data[offset++]);
            if (nudge < -50 || nudge > 50) return false;
        }
        for (uint8_t step = 0U; step < DRUM_MAX_STEPS; ++step) {
            if (data[offset++] > 100U) return false;
        }
    }
    for (uint8_t slot = 0U; slot < DRUM_ADVANCED_ROOT_SLOT_COUNT; ++slot) {
        const uint16_t key = readU16(data, offset);
        if (!validAdvancedStepKey(key, laneCount)) return false;
        if (key == DRUM_ADVANCED_STEP_KEY_INVALID) continue;
        uint16_t previousOffset = static_cast<uint16_t>(
            size - ADVANCED_INDEX_SIZE);
        for (uint8_t previous = 0U; previous < slot; ++previous) {
            if (readU16(data, previousOffset) == key) return false;
        }
    }
    return offset == size;
}

}  // namespace

FLASHMEM bool encodeDrumTrackRecord(
    const DrumTrackState& source,
    uint8_t* out,
    uint16_t size
) {
    if (out == nullptr || size != DRUM_TRACK_RECORD_SIZE ||
        !sourceIsCanonical(source)) {
        return false;
    }

    uint16_t offset = 0U;
    out[offset++] = source.kit.laneCount;
    out[offset++] = source.pattern.defaultLength;
    out[offset++] = source.pattern.defaultStepsPerBeat;
    out[offset++] = 0U;
    writeU32(out, offset, source.kit.revision);
    writeU32(out, offset, source.pattern.revision);
    for (const auto& descriptor : source.kit.lanes) {
        out[offset++] = descriptor.midiNote;
        out[offset++] = static_cast<uint8_t>(descriptor.role);
        out[offset++] = descriptor.overrideMask;
        out[offset++] = static_cast<uint8_t>(descriptor.icon);
        out[offset++] = descriptor.colorIndex;
        for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
            out[offset++] = static_cast<uint8_t>(descriptor.name[i]);
        }
    }
    for (const auto& lane : source.pattern.lanes) {
        out[offset++] = static_cast<uint8_t>(lane.timing.mode);
        out[offset++] = lane.timing.length;
        out[offset++] = lane.timing.stepsPerBeat;
        out[offset++] = 0U;
        for (uint8_t byte = 0U; byte < 16U; ++byte) {
            uint8_t packed = 0U;
            for (uint8_t bit = 0U; bit < 8U; ++bit) {
                const uint8_t step = static_cast<uint8_t>(byte * 8U + bit);
                if (lane.enabledMask.test(step)) {
                    packed = static_cast<uint8_t>(packed | (1U << bit));
                }
            }
            out[offset++] = packed;
        }
        for (const uint8_t value : lane.velocity) out[offset++] = value;
        for (const uint16_t value : lane.gate) writeU16(out, offset, value);
        for (const int8_t value : lane.nudge) {
            out[offset++] = static_cast<uint8_t>(value);
        }
        for (const uint8_t value : lane.probability) out[offset++] = value;
    }
    for (const uint16_t key : source.advancedStepKeys) {
        writeU16(out, offset, key);
    }
    return offset == size;
}

FLASHMEM bool decodeDrumTrackRecord(
    const uint8_t* data,
    uint16_t size,
    DrumTrackState& out
) {
    if (data == nullptr || size != DRUM_TRACK_RECORD_SIZE ||
        !recordIsCanonical(data, size)) {
        return false;
    }

    uint16_t offset = 0U;
    out.kit.laneCount = data[offset++];
    out.pattern.defaultLength = data[offset++];
    out.pattern.defaultStepsPerBeat = data[offset++];
    ++offset;  // reserved
    out.kit.revision = readU32(data, offset);
    out.pattern.revision = readU32(data, offset);
    for (auto& descriptor : out.kit.lanes) {
        descriptor = {};
        descriptor.midiNote = data[offset++];
        descriptor.role = static_cast<DrumLaneRole>(data[offset++]);
        descriptor.overrideMask = data[offset++];
        descriptor.icon = static_cast<DrumLaneIcon>(data[offset++]);
        descriptor.colorIndex = data[offset++];
        for (uint8_t i = 0U; i < DRUM_LANE_NAME_MAX_LENGTH; ++i) {
            descriptor.name[i] = static_cast<char>(data[offset++]);
        }
        descriptor =
            core::state::sequencer::canonicalDrumLaneDescriptor(descriptor);
    }
    for (auto& lane : out.pattern.lanes) {
        lane.timing.mode = static_cast<DrumLaneTimingMode>(data[offset++]);
        lane.timing.length = data[offset++];
        lane.timing.stepsPerBeat = data[offset++];
        ++offset;  // reserved
        lane.enabledMask = {};
        for (uint8_t byte = 0U; byte < 16U; ++byte) {
            const uint8_t packed = data[offset++];
            for (uint8_t bit = 0U; bit < 8U; ++bit) {
                lane.enabledMask.setBit(
                    static_cast<uint8_t>(byte * 8U + bit),
                    (packed & static_cast<uint8_t>(1U << bit)) != 0U
                );
            }
        }
        for (uint8_t& value : lane.velocity) value = data[offset++];
        for (uint16_t& value : lane.gate) value = readU16(data, offset);
        for (int8_t& value : lane.nudge) {
            value = static_cast<int8_t>(data[offset++]);
        }
        for (uint8_t& value : lane.probability) value = data[offset++];
    }
    out.advancedStepKeys.fill(DRUM_ADVANCED_STEP_KEY_INVALID);
    for (uint16_t& key : out.advancedStepKeys) {
        key = readU16(data, offset);
    }
    return offset == size;
}

}  // namespace core::persistence::sequencer_codec
