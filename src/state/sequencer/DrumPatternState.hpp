#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

namespace core::state::sequencer {

/**
 * Cold authored Drum Track domain.
 *
 * A Drum Track owns one MIDI channel outside this type. The kit maps logical
 * lanes to fixed MIDI notes; the pattern owns rhythm and per-step expression.
 * Keeping those responsibilities separate lets a musician change kits without
 * rewriting the rhythm.
 */
inline constexpr uint8_t DRUM_MAX_LANES = 16U;
inline constexpr uint8_t DRUM_DEFAULT_LANE_COUNT = 8U;
inline constexpr uint8_t DRUM_MAX_STEPS = 128U;
inline constexpr uint8_t DRUM_DEFAULT_LENGTH = 8U;
inline constexpr uint8_t DRUM_DEFAULT_STEPS_PER_BEAT = 4U;
inline constexpr uint16_t DRUM_MAX_GATE_PERCENT = 1600U;
inline constexpr uint8_t DRUM_DEFAULT_VELOCITY = 64U;
inline constexpr uint16_t DRUM_DEFAULT_GATE_PERCENT = 100U;
inline constexpr uint8_t DRUM_DEFAULT_PROBABILITY = 100U;

enum class DrumLaneRole : uint8_t {
    CUSTOM = 0,
    KICK,
    SNARE,
    CLOSED_HAT,
    OPEN_HAT,
    CLAP,
    LOW_TOM,
    HIGH_TOM,
    PERCUSSION,
};

const char* drumLaneRoleLabel(DrumLaneRole role);

struct DrumLaneDescriptor {
    uint8_t midiNote = 36U;
    DrumLaneRole role = DrumLaneRole::CUSTOM;
};

struct DrumKitState {
    uint8_t laneCount = DRUM_DEFAULT_LANE_COUNT;
    std::array<DrumLaneDescriptor, DRUM_MAX_LANES> lanes{};
    uint32_t revision = 0U;

    void resetGeneralMidi();
    bool setLaneCount(uint8_t count);
    bool setLane(uint8_t lane, DrumLaneDescriptor descriptor);
};

enum class DrumLaneTimingMode : uint8_t {
    INHERIT_PATTERN = 0,
    CUSTOM,
};

struct DrumLaneTiming {
    DrumLaneTimingMode mode = DrumLaneTimingMode::INHERIT_PATTERN;
    uint8_t length = DRUM_DEFAULT_LENGTH;
    uint8_t stepsPerBeat = DRUM_DEFAULT_STEPS_PER_BEAT;
};

struct DrumLanePattern {
    DrumLaneTiming timing{};
    oc::note::sequencer::StepBitMask128 enabledMask{};
    std::array<uint8_t, DRUM_MAX_STEPS> velocity{};
    std::array<uint16_t, DRUM_MAX_STEPS> gate{};
    std::array<int8_t, DRUM_MAX_STEPS> nudge{};
    std::array<uint8_t, DRUM_MAX_STEPS> probability{};

    void reset();
};

struct DrumPatternState {
    uint8_t defaultLength = DRUM_DEFAULT_LENGTH;
    uint8_t defaultStepsPerBeat = DRUM_DEFAULT_STEPS_PER_BEAT;
    std::array<DrumLanePattern, DRUM_MAX_LANES> lanes{};
    uint32_t revision = 0U;

    void reset();

    [[nodiscard]] uint8_t effectiveLength(uint8_t lane) const;
    [[nodiscard]] uint8_t effectiveStepsPerBeat(uint8_t lane) const;
    [[nodiscard]] bool stepEnabled(uint8_t lane, uint8_t step) const;

    bool setDefaults(uint8_t length, uint8_t stepsPerBeat);
    bool setLaneTimingInherited(uint8_t lane);
    bool setLaneTimingCustom(
        uint8_t lane,
        uint8_t length,
        uint8_t stepsPerBeat
    );
    bool setStepEnabled(uint8_t lane, uint8_t step, bool enabled);
    bool toggleStep(uint8_t lane, uint8_t step);
    bool setStepVelocity(uint8_t lane, uint8_t step, uint8_t velocity);
    bool setStepGate(uint8_t lane, uint8_t step, uint16_t gatePercent);
    bool setStepNudge(uint8_t lane, uint8_t step, int8_t nudgePercent);
    bool setStepProbability(uint8_t lane, uint8_t step, uint8_t probability);
};

struct DrumTrackState {
    DrumKitState kit{};
    DrumPatternState pattern{};

    void reset();
};

/** Immutable, flat hand-off consumed by the realtime Drum engine. */
struct DrumLaneRuntimeSnapshot {
    uint8_t midiNote = 36U;
    uint8_t length = DRUM_DEFAULT_LENGTH;
    uint8_t stepsPerBeat = DRUM_DEFAULT_STEPS_PER_BEAT;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    std::array<uint8_t, DRUM_MAX_STEPS> velocity{};
    std::array<uint16_t, DRUM_MAX_STEPS> gate{};
    std::array<int8_t, DRUM_MAX_STEPS> nudge{};
    std::array<uint8_t, DRUM_MAX_STEPS> probability{};
};

struct DrumPatternRuntimeSnapshot {
    uint8_t laneCount = 0U;
    uint32_t revision = 0U;
    std::array<DrumLaneRuntimeSnapshot, DRUM_MAX_LANES> lanes{};
};

[[nodiscard]] uint32_t drumRuntimeRevision(const DrumTrackState& source);

void captureDrumRuntimeSnapshot(
    const DrumTrackState& source,
    DrumPatternRuntimeSnapshot& out
);

}  // namespace core::state::sequencer
