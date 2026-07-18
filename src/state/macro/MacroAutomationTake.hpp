#pragma once

#include <array>
#include <cstdint>

#include "state/macro/MacroAutomationDomain.hpp"

namespace core::state::macro {

/** Musical duration selected before one shared Macro Automation take. */
enum class MacroAutomationTakeTiming : uint8_t {
    HOLD = 0,
    NOTE_1_16,
    NOTE_1_8,
    NOTE_1_4,
    NOTE_1_2,
    BAR_1,
    BARS_2,
    BARS_4,
    BARS_8,
    BARS_16,
    BARS_32,
};

enum class MacroAutomationTakePhase : uint8_t {
    IDLE = 0,
    ARMED,
    RECORDING,
};

inline constexpr uint8_t MACRO_AUTOMATION_TAKE_TIMING_COUNT = 11U;

[[nodiscard]] const char* macroAutomationTakeTimingLabel(
    MacroAutomationTakeTiming timing
);
[[nodiscard]] uint16_t macroAutomationTakeFixedDurationTicks(
    MacroAutomationTakeTiming timing
);
[[nodiscard]] MacroAutomationTakeTiming nextMacroAutomationTakeTiming(
    MacroAutomationTakeTiming timing,
    int delta
);
[[nodiscard]] uint16_t macroAutomationTakeQuantizeHoldTicks(uint32_t rawTicks);

/**
 * One bounded, shared-time performance scratch.
 *
 * The enclosing UiSystemState is allocated in EXTMEM. One time axis and eight
 * MIDI-7 columns replace eight 16 KiB float lanes. Recording and decimation
 * perform no allocation and always retain inter-Macro phase relationships.
 */
struct MacroAutomationTakeState {
    static constexpr uint16_t SAMPLE_CAPACITY =
        MACRO_AUTOMATION_RECORDING_MAX_POINTS;
    static constexpr uint8_t VALUE_COLUMN_COUNT = 8U;

    std::array<uint16_t, SAMPLE_CAPACITY> elapsedTicks{};
    std::array<
        std::array<uint8_t, SAMPLE_CAPACITY>,
        VALUE_COLUMN_COUNT
    > values{};
    std::array<uint8_t, VALUE_COLUMN_COUNT> initialValues{};
    std::array<uint8_t, VALUE_COLUMN_COUNT> currentValues{};
    std::array<float, VALUE_COLUMN_COUNT> previousManualValues{};
    uint32_t startedAtMs = 0U;
    uint32_t startedMusicalTick = 0U;
    uint32_t transportGeneration = 0U;
    uint32_t authoredRevision = 0U;
    uint32_t startProjectPhaseTick = 0U;
    uint16_t durationTicks = 0U;
    uint16_t sampleCount = 0U;
    uint16_t candidateMask = 0U;
    uint16_t touchedMask = 0U;
    uint16_t changedMask = 0U;
    uint16_t manualRestoreMask = 0U;
    MacroAutomationTakeTiming timing = MacroAutomationTakeTiming::HOLD;
    MacroAutomationTakePhase phase = MacroAutomationTakePhase::IDLE;
    uint8_t track = 0U;
    uint8_t page = 0U;
    bool reduced = false;

    void reset();
    void arm(MacroAutomationTakeTiming selectedTiming,
             uint16_t candidates,
             const std::array<uint8_t, VALUE_COLUMN_COUNT>& bases);
    [[nodiscard]] bool begin(uint32_t nowMs,
                             uint32_t musicalTick,
                             uint32_t projectPhaseTick,
                             uint32_t generation,
                             uint32_t revision);
    [[nodiscard]] bool touch(uint8_t macro, uint8_t value, uint32_t elapsedTick);
    [[nodiscard]] bool sample(uint32_t elapsedTick);
    [[nodiscard]] bool finish(uint32_t elapsedTick);
    [[nodiscard]] bool fixedLength() const;
    [[nodiscard]] bool completeAt(uint32_t elapsedTick) const;
    [[nodiscard]] uint16_t playbackWindowOffsetTicks() const;
    [[nodiscard]] bool activeFor(uint8_t macro) const;
    [[nodiscard]] float latestBase(uint8_t macro) const;

    /** Builds one rationalized absolute curve into caller-owned cold storage. */
    [[nodiscard]] bool buildPackedCurve(
        uint8_t macro,
        MacroPackedCurvePoint* output,
        uint16_t capacity,
        uint16_t& written
    ) const;

private:
    void decimate_();
};

static_assert(MacroAutomationTakeState::VALUE_COLUMN_COUNT == 8U);
static_assert(sizeof(MacroAutomationTakeState) <= 21U * 1024U);

}  // namespace core::state::macro
