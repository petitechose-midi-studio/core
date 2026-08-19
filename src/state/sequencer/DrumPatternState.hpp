#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

namespace core::state::sequencer {

struct SequencerPatternState;

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
inline constexpr uint8_t DRUM_LANE_NAME_MAX_LENGTH = 8U;
inline constexpr uint8_t DRUM_LANE_COLOR_COUNT = 8U;
// One Track reuses the 128 canonical root nodes of its existing Sequencer
// Graph. Only Drum steps that own advanced content consume a slot.
inline constexpr uint8_t DRUM_ADVANCED_ROOT_SLOT_COUNT = 128U;
inline constexpr uint16_t DRUM_ADVANCED_STEP_KEY_INVALID = 0xFFFFU;

/**
 * Compact, viewport-scoped preview of advanced Drum content.
 *
 * The main loop resolves only the visible 8 x 8 page for the current Lane
 * cycles. This keeps Graph traversal out of LVGL drawing and avoids retaining
 * a 16 x 128 telemetry matrix for a view that can display only 64 cells.
 */
struct DrumResolvedPageProjection {
    static constexpr uint8_t VISIBLE_LANES = 8U;
    static constexpr uint8_t STEPS_PER_PAGE = 8U;
    static constexpr std::size_t CELL_COUNT =
        static_cast<std::size_t>(VISIBLE_LANES) * STEPS_PER_PAGE;

    std::array<uint8_t, CELL_COUNT> velocity{};
    std::array<uint16_t, CELL_COUNT> gate{};
    std::array<int8_t, CELL_COUNT> nudge{};
    // Current-loop rhythmic summary for a top-level MicroSequence. Sixteen
    // bounded bits map exactly to the engine's maximum expansion length.
    std::array<uint16_t, CELL_COUNT> microMask{};
    std::array<uint8_t, CELL_COUNT> microLength{};
    uint64_t cyclePresentMask = 0U;
    uint64_t validMask = 0U;
    uint64_t playedMask = 0U;
    uint16_t contextKey = UINT16_MAX;

    void reset() {
        velocity.fill(0U);
        gate.fill(0U);
        nudge.fill(0);
        microMask.fill(0U);
        microLength.fill(0U);
        cyclePresentMask = 0U;
        validMask = 0U;
        playedMask = 0U;
        contextKey = UINT16_MAX;
    }

    [[nodiscard]] static constexpr std::size_t cellIndex(
        uint8_t row,
        uint8_t column
    ) {
        return static_cast<std::size_t>(row) * STEPS_PER_PAGE + column;
    }

    [[nodiscard]] static constexpr uint64_t cellBit(
        uint8_t row,
        uint8_t column
    ) {
        return UINT64_C(1) << cellIndex(row, column);
    }

    [[nodiscard]] bool matches(
        const DrumResolvedPageProjection& other
    ) const {
        return velocity == other.velocity && gate == other.gate &&
            nudge == other.nudge && microMask == other.microMask &&
            microLength == other.microLength &&
            cyclePresentMask == other.cyclePresentMask &&
            validMask == other.validMask &&
            playedMask == other.playedMask && contextKey == other.contextKey;
    }
};

static_assert(
    sizeof(DrumResolvedPageProjection) <= 480U,
    "visible Drum resolved projection must remain compact"
);

enum class DrumKitPreset : uint8_t {
    EMPTY = 0,
    GENERAL_MIDI,
    COUNT,
};

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

enum class DrumLaneIcon : uint8_t {
    GENERIC = 0,
    KICK,
    SNARE,
    CLOSED_HAT,
    OPEN_HAT,
    CLAP,
    TOM,
    PERCUSSION,
    COUNT,
};

inline constexpr uint8_t DRUM_LANE_OVERRIDE_NAME = 1U << 0U;
inline constexpr uint8_t DRUM_LANE_OVERRIDE_ICON = 1U << 1U;
inline constexpr uint8_t DRUM_LANE_OVERRIDE_COLOR = 1U << 2U;
inline constexpr uint8_t DRUM_LANE_OVERRIDE_ALL =
    DRUM_LANE_OVERRIDE_NAME |
    DRUM_LANE_OVERRIDE_ICON |
    DRUM_LANE_OVERRIDE_COLOR;

struct DrumLaneRolePreset {
    const char* name = "Lane";
    DrumLaneIcon icon = DrumLaneIcon::GENERIC;
    uint8_t colorIndex = 0U;
};

const char* drumLaneRoleLabel(DrumLaneRole role);
const char* drumKitPresetLabel(DrumKitPreset preset);
DrumLaneIcon drumLaneDefaultIcon(DrumLaneRole role);
uint8_t drumLaneDefaultColorIndex(DrumLaneRole role);
DrumLaneRolePreset drumLaneRolePreset(DrumLaneRole role);
const char* drumLaneIconLabel(DrumLaneIcon icon);

struct DrumLaneDescriptor {
    uint8_t midiNote = 36U;
    DrumLaneRole role = DrumLaneRole::CUSTOM;
    uint8_t overrideMask = 0U;
    // These three fields are authored override storage only. Accessors below
    // resolve the immutable role preset when the matching bit is absent.
    DrumLaneIcon icon = DrumLaneIcon::GENERIC;
    uint8_t colorIndex = 0U;
    // The extra byte is never serialized and guarantees safe display.
    std::array<char, DRUM_LANE_NAME_MAX_LENGTH + 1U> name{};
};

DrumLaneDescriptor canonicalDrumLaneDescriptor(
    DrumLaneDescriptor descriptor
);
const char* drumLaneDisplayName(const DrumLaneDescriptor& descriptor);
DrumLaneIcon drumLaneDisplayIcon(const DrumLaneDescriptor& descriptor);
uint8_t drumLaneDisplayColorIndex(const DrumLaneDescriptor& descriptor);
bool setDrumLaneRole(DrumLaneDescriptor& descriptor, DrumLaneRole role);
bool setDrumLaneName(DrumLaneDescriptor& descriptor, const char* name);
bool setDrumLaneIcon(DrumLaneDescriptor& descriptor, DrumLaneIcon icon);
bool setDrumLaneColorIndex(DrumLaneDescriptor& descriptor, uint8_t colorIndex);
uint8_t drumLaneIdentityOverrideCount(const DrumLaneDescriptor& descriptor);
bool resetDrumLaneIdentityOverrides(DrumLaneDescriptor& descriptor);

struct DrumKitState {
    uint8_t laneCount = DRUM_DEFAULT_LANE_COUNT;
    std::array<DrumLaneDescriptor, DRUM_MAX_LANES> lanes{};
    uint32_t revision = 0U;

    void resetEmpty();
    void resetGeneralMidi();
    void applyPreset(DrumKitPreset preset);
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

[[nodiscard]] bool sameDrumLanePattern(
    const DrumLanePattern& lhs,
    const DrumLanePattern& rhs
);

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
    bool replaceLanePattern(uint8_t lane, const DrumLanePattern& source);
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
    // slot -> packed (lane << 7 | step). The Graph remains owned by the
    // corresponding SequencerPatternState, so this cold index is the only
    // Drum-specific storage cost for MicroSequence/Cycle States.
    std::array<uint16_t, DRUM_ADVANCED_ROOT_SLOT_COUNT> advancedStepKeys{};

    void reset(DrumKitPreset preset = DrumKitPreset::GENERAL_MIDI);
    bool insertLane(uint8_t index, DrumLaneDescriptor descriptor);
    bool appendLane(DrumLaneDescriptor descriptor);
    bool removeLane(uint8_t index);
    bool moveLane(uint8_t from, uint8_t to);
    [[nodiscard]] int16_t advancedRootSlot(uint8_t lane, uint8_t step) const;
    [[nodiscard]] int16_t firstFreeAdvancedRootSlot() const;
    bool bindAdvancedRootSlot(uint8_t slot, uint8_t lane, uint8_t step);
    bool releaseAdvancedRootSlot(uint8_t lane, uint8_t step);
};

/**
 * Resolve or reserve the canonical Pattern-Graph root used by one Drum Step.
 *
 * The operation only changes the cold Drum mapping. Graph payload allocation
 * remains the caller's transaction so a failed copy/create can roll back the
 * full Drum + Graph history snapshot atomically.
 */
[[nodiscard]] int16_t ensureDrumAdvancedRootSlot(
    DrumTrackState& drumTrack,
    const SequencerPatternState& pattern,
    uint8_t lane,
    uint8_t step,
    bool& mappingChanged
);

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
    std::array<uint16_t, DRUM_ADVANCED_ROOT_SLOT_COUNT> advancedStepKeys{};
    std::array<DrumLaneRuntimeSnapshot, DRUM_MAX_LANES> lanes{};

    [[nodiscard]] int16_t advancedRootSlot(uint8_t lane, uint8_t step) const;
};

[[nodiscard]] uint32_t drumRuntimeRevision(const DrumTrackState& source);

void captureDrumRuntimeSnapshot(
    const DrumTrackState& source,
    DrumPatternRuntimeSnapshot& out
);

}  // namespace core::state::sequencer
