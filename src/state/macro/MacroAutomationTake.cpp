#include "state/macro/MacroAutomationTake.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

namespace {

const char kTimingHold[] PROGMEM = "HOLD";
const char kTiming1_16[] PROGMEM = "1/16";
const char kTiming1_8[] PROGMEM = "1/8";
const char kTiming1_4[] PROGMEM = "1/4";
const char kTiming1_2[] PROGMEM = "1/2";
const char kTiming1Bar[] PROGMEM = "1 BAR";
const char kTiming2Bars[] PROGMEM = "2 BARS";
const char kTiming4Bars[] PROGMEM = "4 BARS";
const char kTiming8Bars[] PROGMEM = "8 BARS";
const char kTiming16Bars[] PROGMEM = "16 BARS";
const char kTiming32Bars[] PROGMEM = "32 BARS";

const char* const kTimingLabels[MACRO_AUTOMATION_TAKE_TIMING_COUNT] PROGMEM{
    kTimingHold,
    kTiming1_16,
    kTiming1_8,
    kTiming1_4,
    kTiming1_2,
    kTiming1Bar,
    kTiming2Bars,
    kTiming4Bars,
    kTiming8Bars,
    kTiming16Bars,
    kTiming32Bars,
};

const uint16_t kTimingTicks[MACRO_AUTOMATION_TAKE_TIMING_COUNT] PROGMEM{
        0U,
        48U,
        96U,
        192U,
        384U,
        768U,
        1536U,
        3072U,
        6144U,
        12288U,
        24576U,
};

constexpr int32_t kPackedMidi7HalfStepError = 130;

uint8_t timingIndex(MacroAutomationTakeTiming timing) {
    const auto index = static_cast<uint8_t>(timing);
    return index < MACRO_AUTOMATION_TAKE_TIMING_COUNT ? index : 0U;
}

int16_t packMidi7(uint8_t value) {
    const uint8_t bounded = std::min<uint8_t>(value, 127U);
    return static_cast<int16_t>(
        (static_cast<uint32_t>(bounded) * 32767U + 63U) / 127U
    );
}

bool segmentFits(
    const MacroPackedCurvePoint* points,
    uint16_t start,
    uint16_t end
) {
    if (points == nullptr || end <= static_cast<uint16_t>(start + 1U)) {
        return true;
    }
    const auto& first = points[start];
    const auto& last = points[end];
    const int32_t span = static_cast<int32_t>(last.tick) - first.tick;
    if (span <= 0) return false;
    for (uint16_t index = static_cast<uint16_t>(start + 1U);
         index < end;
         ++index) {
        const auto& point = points[index];
        const int32_t position = static_cast<int32_t>(point.tick) - first.tick;
        const int64_t valueDelta =
            static_cast<int32_t>(last.value) - first.value;
        const int32_t expected = static_cast<int32_t>(
            static_cast<int64_t>(first.value) +
            (valueDelta * position + (span / 2)) / span
        );
        if (std::abs(static_cast<int32_t>(point.value) - expected) >
            kPackedMidi7HalfStepError) {
            return false;
        }
    }
    return true;
}

uint16_t simplifyPacked(MacroPackedCurvePoint* points, uint16_t count) {
    if (points == nullptr || count <= 2U) return count;
    uint16_t write = 1U;
    uint16_t anchor = 0U;
    uint16_t candidate = 2U;
    while (candidate < count) {
        if (segmentFits(points, anchor, candidate)) {
            ++candidate;
            continue;
        }
        const uint16_t keep = static_cast<uint16_t>(candidate - 1U);
        points[write++] = points[keep];
        anchor = keep;
        candidate = static_cast<uint16_t>(anchor + 2U);
    }
    const auto last = points[static_cast<uint16_t>(count - 1U)];
    if (points[static_cast<uint16_t>(write - 1U)].tick != last.tick ||
        points[static_cast<uint16_t>(write - 1U)].value != last.value) {
        points[write++] = last;
    }
    return write;
}

}  // namespace

FLASHMEM const char* macroAutomationTakeTimingLabel(
    MacroAutomationTakeTiming timing
) {
    return kTimingLabels[timingIndex(timing)];
}

FLASHMEM uint16_t macroAutomationTakeFixedDurationTicks(
    MacroAutomationTakeTiming timing
) {
    return kTimingTicks[timingIndex(timing)];
}

FLASHMEM MacroAutomationTakeTiming nextMacroAutomationTakeTiming(
    MacroAutomationTakeTiming timing,
    int delta
) {
    const int count = static_cast<int>(MACRO_AUTOMATION_TAKE_TIMING_COUNT);
    int index = static_cast<int>(timingIndex(timing));
    index = ((index + delta) % count + count) % count;
    return static_cast<MacroAutomationTakeTiming>(index);
}

FLASHMEM uint16_t macroAutomationTakeQuantizeHoldTicks(uint32_t rawTicks) {
    const uint32_t bounded = std::clamp<uint32_t>(rawTicks, 1U, UINT16_MAX);
    if (bounded > kTimingTicks[5]) {
        const uint32_t bars = std::max<uint32_t>(
            1U,
            (bounded + 384U) / 768U
        );
        return static_cast<uint16_t>(std::min<uint32_t>(
            bars * 768U,
            UINT16_MAX
        ));
    }
    uint16_t best = kTimingTicks[1];
    uint32_t distance = static_cast<uint32_t>(
        std::abs(static_cast<int32_t>(bounded) - best)
    );
    for (uint8_t index = 2U; index <= 5U; ++index) {
        const uint16_t candidate = kTimingTicks[index];
        const uint32_t candidateDistance = static_cast<uint32_t>(
            std::abs(static_cast<int32_t>(bounded) - candidate)
        );
        if (candidateDistance <= distance) {
            best = candidate;
            distance = candidateDistance;
        }
    }
    return best;
}

FLASHMEM void MacroAutomationTakeState::reset() {
    startedAtMs = 0U;
    startedMusicalTick = 0U;
    transportGeneration = 0U;
    authoredRevision = 0U;
    startProjectPhaseTick = 0U;
    durationTicks = 0U;
    sampleCount = 0U;
    candidateMask = 0U;
    touchedMask = 0U;
    changedMask = 0U;
    manualRestoreMask = 0U;
    timing = MacroAutomationTakeTiming::HOLD;
    phase = MacroAutomationTakePhase::IDLE;
    track = 0U;
    page = 0U;
    reduced = false;
    initialValues.fill(0U);
    currentValues.fill(0U);
    previousManualValues.fill(0.0f);
}

FLASHMEM void MacroAutomationTakeState::arm(
    MacroAutomationTakeTiming selectedTiming,
    uint16_t candidates,
    const std::array<uint8_t, VALUE_COLUMN_COUNT>& bases
) {
    reset();
    timing = selectedTiming;
    candidateMask = static_cast<uint16_t>(candidates & 0x00FFU);
    for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
        initialValues[macro] = std::min<uint8_t>(bases[macro], 127U);
    }
    currentValues = initialValues;
    durationTicks = macroAutomationTakeFixedDurationTicks(timing);
    phase = MacroAutomationTakePhase::ARMED;
}

FLASHMEM bool MacroAutomationTakeState::begin(
    uint32_t nowMs,
    uint32_t musicalTick,
    uint32_t projectPhaseTick,
    uint32_t generation,
    uint32_t revision
) {
    if (phase != MacroAutomationTakePhase::ARMED || candidateMask == 0U) {
        return false;
    }
    startedAtMs = nowMs;
    startedMusicalTick = musicalTick;
    startProjectPhaseTick = projectPhaseTick;
    transportGeneration = generation;
    authoredRevision = revision;
    sampleCount = 1U;
    elapsedTicks[0] = 0U;
    for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
        values[macro][0] = currentValues[macro];
    }
    phase = MacroAutomationTakePhase::RECORDING;
    return true;
}

FLASHMEM bool MacroAutomationTakeState::touch(
    uint8_t macro,
    uint8_t value,
    uint32_t elapsedTick
) {
    if (phase != MacroAutomationTakePhase::RECORDING ||
        macro >= VALUE_COLUMN_COUNT ||
        (candidateMask & static_cast<uint16_t>(1U << macro)) == 0U) {
        return false;
    }
    const uint8_t bounded = std::min<uint8_t>(value, 127U);
    const uint16_t bit = static_cast<uint16_t>(1U << macro);
    touchedMask = static_cast<uint16_t>(touchedMask | bit);
    if (bounded != initialValues[macro]) {
        changedMask = static_cast<uint16_t>(changedMask | bit);
    }
    currentValues[macro] = bounded;
    return sample(elapsedTick);
}

FLASHMEM bool MacroAutomationTakeState::sample(uint32_t elapsedTick) {
    if (phase != MacroAutomationTakePhase::RECORDING) return false;
    uint16_t tick = static_cast<uint16_t>(std::min<uint32_t>(
        elapsedTick,
        fixedLength() ? durationTicks : UINT16_MAX
    ));
    if (sampleCount > 0U) {
        const uint16_t last = elapsedTicks[static_cast<uint16_t>(sampleCount - 1U)];
        if (tick < last) return false;
        if (tick == last) {
            for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
                values[macro][static_cast<uint16_t>(sampleCount - 1U)] =
                    currentValues[macro];
            }
            return true;
        }
    }
    if (sampleCount >= SAMPLE_CAPACITY) decimate_();
    if (sampleCount >= SAMPLE_CAPACITY) return false;
    elapsedTicks[sampleCount] = tick;
    for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
        values[macro][sampleCount] = currentValues[macro];
    }
    ++sampleCount;
    return true;
}

FLASHMEM bool MacroAutomationTakeState::finish(uint32_t elapsedTick) {
    if (phase != MacroAutomationTakePhase::RECORDING || touchedMask == 0U) {
        return false;
    }
    const uint32_t raw = std::max<uint32_t>(elapsedTick, 1U);
    durationTicks = fixedLength()
        ? macroAutomationTakeFixedDurationTicks(timing)
        : macroAutomationTakeQuantizeHoldTicks(raw);
    const uint32_t terminal = fixedLength()
        ? durationTicks
        : std::min<uint32_t>(raw, UINT16_MAX);
    return sample(terminal);
}

FLASHMEM bool MacroAutomationTakeState::fixedLength() const {
    return timing != MacroAutomationTakeTiming::HOLD;
}

FLASHMEM bool MacroAutomationTakeState::completeAt(uint32_t elapsedTick) const {
    return phase == MacroAutomationTakePhase::RECORDING && fixedLength() &&
           elapsedTick >= durationTicks;
}

FLASHMEM uint16_t MacroAutomationTakeState::playbackWindowOffsetTicks() const {
    if (durationTicks == 0U) return 0U;
    const uint16_t phaseTick = static_cast<uint16_t>(
        startProjectPhaseTick % durationTicks
    );
    return phaseTick == 0U
        ? 0U
        : static_cast<uint16_t>(durationTicks - phaseTick);
}

FLASHMEM bool MacroAutomationTakeState::activeFor(uint8_t macro) const {
    return macro < VALUE_COLUMN_COUNT &&
           (touchedMask & static_cast<uint16_t>(1U << macro)) != 0U;
}

FLASHMEM float MacroAutomationTakeState::latestBase(uint8_t macro) const {
    if (macro >= VALUE_COLUMN_COUNT) return 0.0f;
    return static_cast<float>(currentValues[macro]) / 127.0f;
}

FLASHMEM bool MacroAutomationTakeState::buildPackedCurve(
    uint8_t macro,
    MacroPackedCurvePoint* output,
    uint16_t capacity,
    uint16_t& written
) const {
    written = 0U;
    if (phase != MacroAutomationTakePhase::RECORDING ||
        macro >= VALUE_COLUMN_COUNT || !activeFor(macro) ||
        output == nullptr || capacity == 0U || sampleCount == 0U ||
        durationTicks == 0U) {
        return false;
    }
    const uint16_t rawDuration = std::max<uint16_t>(
        elapsedTicks[static_cast<uint16_t>(sampleCount - 1U)],
        1U
    );
    for (uint16_t index = 0U; index < sampleCount; ++index) {
        const uint16_t tick = static_cast<uint16_t>(std::min<uint32_t>(
            (static_cast<uint32_t>(elapsedTicks[index]) * durationTicks +
             rawDuration / 2U) /
                rawDuration,
            durationTicks
        ));
        const int16_t value = packMidi7(values[macro][index]);
        if (written > 0U && output[static_cast<uint16_t>(written - 1U)].tick == tick) {
            output[static_cast<uint16_t>(written - 1U)].value = value;
            continue;
        }
        if (written >= capacity) return false;
        output[written++] = {tick, value};
    }
    if (written == 0U) return false;
    output[0].tick = 0U;
    if (written == 1U) {
        return true;
    }
    output[static_cast<uint16_t>(written - 1U)].tick = durationTicks;
    written = simplifyPacked(output, written);
    return written > 0U;
}

FLASHMEM void MacroAutomationTakeState::decimate_() {
    if (sampleCount < 3U) return;
    const uint16_t original = sampleCount;
    uint16_t write = 1U;
    for (uint16_t read = 2U;
         static_cast<uint16_t>(read + 1U) < original;
         read = static_cast<uint16_t>(read + 2U)) {
        elapsedTicks[write] = elapsedTicks[read];
        for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
            values[macro][write] = values[macro][read];
        }
        ++write;
    }
    const uint16_t last = static_cast<uint16_t>(original - 1U);
    elapsedTicks[write] = elapsedTicks[last];
    for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
        values[macro][write] = values[macro][last];
    }
    sampleCount = static_cast<uint16_t>(write + 1U);
    reduced = true;
}

}  // namespace core::state::macro
