#include "state/macro/MacroAutomationTake.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

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

uint16_t simplifyPacked(
    core::state::modulation::ProjectPackedCurvePoint* points,
    uint16_t count
) {
    if (points == nullptr || count <= 2U) return count;
    OC_PERF_SCOPE(perfSimplify, "macro.take.commit.simplify");

    // Linear-time bounded-error corridor. The previous implementation retried
    // every progressively longer segment and became quadratic for dense,
    // near-linear 2,048-point overdub grids.
    uint16_t write = 1U;
    uint16_t anchor = 0U;
    uint16_t candidate = 1U;
    double lowerSlope = -std::numeric_limits<double>::infinity();
    double upperSlope = std::numeric_limits<double>::infinity();
    while (candidate < count) {
        const int32_t span = static_cast<int32_t>(points[candidate].tick) -
            points[anchor].tick;
        if (span <= 0) {
            points[write++] = points[candidate++];
            anchor = static_cast<uint16_t>(candidate - 1U);
            lowerSlope = -std::numeric_limits<double>::infinity();
            upperSlope = std::numeric_limits<double>::infinity();
            continue;
        }
        const double delta = static_cast<double>(
            static_cast<int32_t>(points[candidate].value) - points[anchor].value
        );
        const double slope = delta / static_cast<double>(span);
        if (candidate == static_cast<uint16_t>(anchor + 1U) ||
            (slope >= lowerSlope && slope <= upperSlope)) {
            lowerSlope = std::max(
                lowerSlope,
                (delta - kPackedMidi7HalfStepError) /
                    static_cast<double>(span)
            );
            upperSlope = std::min(
                upperSlope,
                (delta + kPackedMidi7HalfStepError) /
                    static_cast<double>(span)
            );
            ++candidate;
            continue;
        }
        const uint16_t keep = static_cast<uint16_t>(candidate - 1U);
        points[write++] = points[keep];
        anchor = keep;
        candidate = static_cast<uint16_t>(anchor + 1U);
        lowerSlope = -std::numeric_limits<double>::infinity();
        upperSlope = std::numeric_limits<double>::infinity();
    }
    const auto last = points[static_cast<uint16_t>(count - 1U)];
    if (points[static_cast<uint16_t>(write - 1U)].tick != last.tick ||
        points[static_cast<uint16_t>(write - 1U)].value != last.value) {
        points[write++] = last;
    }
    OC_PERF_UNITS(perfSimplify, count, write);
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
    writeCursorMask = 0U;
    latestElapsedTick = 0U;
    scratchCurveRevision = 0U;
    timing = MacroAutomationTakeTiming::HOLD;
    phase = MacroAutomationTakePhase::IDLE;
    track = 0U;
    page = 0U;
    circular = false;
    reduced = false;
    initialValues.fill(0U);
    currentValues.fill(0U);
    lastWriteValues.fill(0U);
    lastWriteElapsedTicks.fill(0U);
    previousManualValues.fill(0.0f);
}

FLASHMEM void MacroAutomationTakeState::arm(
    MacroAutomationTakeTiming selectedTiming,
    uint16_t candidates,
    const std::array<uint8_t, VALUE_COLUMN_COUNT>& bases
) {
    reset();
    timing = selectedTiming;
    circular = timing != MacroAutomationTakeTiming::HOLD;
    candidateMask = static_cast<uint16_t>(candidates & 0x00FFU);
    for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
        initialValues[macro] = std::min<uint8_t>(bases[macro], 127U);
    }
    currentValues = initialValues;
    lastWriteValues = initialValues;
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
    latestElapsedTick = 0U;
    if (fixedLength()) {
        initializeFixedGrid_();
    } else {
        sampleCount = 1U;
        elapsedTicks[0] = 0U;
        for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
            values[macro][0] = currentValues[macro];
        }
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
    const uint16_t bit = static_cast<uint16_t>(1U << macro);
    const uint8_t bounded = std::min<uint8_t>(value, 127U);
    if (fixedLength()) {
        const uint32_t monotoneElapsed = std::max(
            elapsedTick,
            latestElapsedTick
        );
        // Every previously joined Macro advances on the same clock. The Macro
        // receiving this encoder event interpolates to the new value; other
        // columns retain their current value over the same traversed region.
        for (uint8_t candidate = 0U;
             candidate < VALUE_COLUMN_COUNT;
             ++candidate) {
            const uint16_t candidateBit = static_cast<uint16_t>(1U << candidate);
            if (candidate != macro && (touchedMask & candidateBit) != 0U &&
                !sampleFixedColumn_(
                    candidate,
                    monotoneElapsed,
                    currentValues[candidate]
                )) {
                return false;
            }
        }
        touchedMask = static_cast<uint16_t>(touchedMask | bit);
        currentValues[macro] = bounded;
        if (!sampleFixedColumn_(macro, monotoneElapsed, bounded)) return false;
        latestElapsedTick = monotoneElapsed;
        return true;
    }

    touchedMask = static_cast<uint16_t>(touchedMask | bit);
    if (bounded != initialValues[macro]) {
        changedMask = static_cast<uint16_t>(changedMask | bit);
    }
    currentValues[macro] = bounded;
    return sample(elapsedTick);
}

FLASHMEM bool MacroAutomationTakeState::sample(uint32_t elapsedTick) {
    if (phase != MacroAutomationTakePhase::RECORDING) return false;
    if (fixedLength()) {
        const uint32_t monotoneElapsed = std::max(
            elapsedTick,
            latestElapsedTick
        );
        for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
            const uint16_t bit = static_cast<uint16_t>(1U << macro);
            if ((touchedMask & bit) != 0U &&
                !sampleFixedColumn_(macro, monotoneElapsed, currentValues[macro])) {
                return false;
            }
        }
        latestElapsedTick = monotoneElapsed;
        return true;
    }
    uint16_t tick = static_cast<uint16_t>(std::min<uint32_t>(
        elapsedTick,
        UINT16_MAX
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
    if (!fixedLength()) {
        durationTicks = macroAutomationTakeQuantizeHoldTicks(raw);
    }
    const uint32_t terminal = fixedLength()
        ? raw
        : std::min<uint32_t>(raw, UINT16_MAX);
    return sample(terminal);
}

FLASHMEM bool MacroAutomationTakeState::fixedLength() const {
    return circular;
}

FLASHMEM bool MacroAutomationTakeState::overrideFixedDuration(uint16_t ticks) {
    if (phase != MacroAutomationTakePhase::ARMED || ticks == 0U) return false;
    circular = true;
    durationTicks = ticks;
    return true;
}

FLASHMEM uint16_t MacroAutomationTakeState::playbackWindowOffsetTicks() const {
    if (durationTicks == 0U) return 0U;
    // Fixed takes are authored directly in Project timeline phase coordinates.
    if (fixedLength()) return 0U;
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

FLASHMEM bool MacroAutomationTakeState::sampleFixedPreviewValue(
    uint8_t macro,
    uint16_t positionQ16,
    float& value
) const {
    if (phase != MacroAutomationTakePhase::RECORDING || !fixedLength() ||
        !activeFor(macro) || sampleCount < 2U) {
        return false;
    }
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint32_t scaled =
        static_cast<uint32_t>(positionQ16) * intervals;
    const uint16_t lower = static_cast<uint16_t>(scaled / 65535U);
    const uint16_t upper = std::min<uint16_t>(
        static_cast<uint16_t>(lower + 1U),
        intervals
    );
    const uint32_t fraction = scaled % 65535U;
    const uint32_t inverse = 65535U - fraction;
    const uint32_t interpolated =
        static_cast<uint32_t>(values[macro][lower]) * inverse +
        static_cast<uint32_t>(values[macro][upper]) * fraction;
    value = static_cast<float>(interpolated) /
        (65535.0f * 127.0f);
    return true;
}

FLASHMEM bool MacroAutomationTakeState::fixedWritePositionQ16(
    uint8_t macro,
    uint16_t& positionQ16
) const {
    if (phase != MacroAutomationTakePhase::RECORDING || !fixedLength() ||
        !activeFor(macro) || durationTicks == 0U) {
        return false;
    }
    const uint32_t phaseTick = static_cast<uint32_t>(
        (static_cast<uint64_t>(startProjectPhaseTick % durationTicks) +
         static_cast<uint64_t>(latestElapsedTick % durationTicks)) %
        durationTicks
    );
    positionQ16 = static_cast<uint16_t>(
        (static_cast<uint64_t>(phaseTick) * 65535U + durationTicks / 2U) /
        durationTicks
    );
    return true;
}

FLASHMEM bool MacroAutomationTakeState::seedFixedGridValue(
    uint8_t macro,
    uint16_t sample,
    uint8_t value
) {
    if (phase != MacroAutomationTakePhase::RECORDING || !fixedLength() ||
        macro >= VALUE_COLUMN_COUNT || sample >= sampleCount ||
        (touchedMask & static_cast<uint16_t>(1U << macro)) != 0U) {
        return false;
    }
    const uint8_t bounded = std::min<uint8_t>(value, 127U);
    if (values[macro][sample] != bounded) {
        values[macro][sample] = bounded;
        ++scratchCurveRevision;
    }
    return true;
}

FLASHMEM bool MacroAutomationTakeState::buildPackedCurve(
    uint8_t macro,
    core::state::modulation::ProjectPackedCurvePoint* output,
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
    const uint16_t rawDuration = fixedLength()
        ? durationTicks
        : std::max<uint16_t>(
              elapsedTicks[static_cast<uint16_t>(sampleCount - 1U)],
              1U
          );
    for (uint16_t index = 0U; index < sampleCount; ++index) {
        const uint16_t tick = fixedLength()
            ? elapsedTicks[index]
            : static_cast<uint16_t>(std::min<uint32_t>(
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

FLASHMEM void MacroAutomationTakeState::initializeFixedGrid_() {
    const uint32_t requested = static_cast<uint32_t>(durationTicks) + 1U;
    sampleCount = static_cast<uint16_t>(std::min<uint32_t>(
        requested,
        SAMPLE_CAPACITY
    ));
    if (sampleCount < 2U) sampleCount = 2U;
    reduced = requested > SAMPLE_CAPACITY;
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    for (uint16_t sample = 0U; sample < sampleCount; ++sample) {
        elapsedTicks[sample] = static_cast<uint16_t>(
            (static_cast<uint32_t>(sample) * durationTicks + intervals / 2U) /
            intervals
        );
        for (uint8_t macro = 0U; macro < VALUE_COLUMN_COUNT; ++macro) {
            values[macro][sample] = initialValues[macro];
        }
    }
}

FLASHMEM void MacroAutomationTakeState::writeFixedGridValue_(
    uint8_t macro,
    uint64_t absoluteGridOrdinal,
    uint8_t value
) {
    if (macro >= VALUE_COLUMN_COUNT || sampleCount < 2U) return;
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint16_t sample = static_cast<uint16_t>(
        absoluteGridOrdinal % intervals
    );
    const uint8_t bounded = std::min<uint8_t>(value, 127U);
    const bool endpointChanged = sample == 0U &&
        values[macro][intervals] != bounded;
    if (values[macro][sample] != bounded || endpointChanged) {
        changedMask = static_cast<uint16_t>(
            changedMask | static_cast<uint16_t>(1U << macro)
        );
        ++scratchCurveRevision;
    }
    values[macro][sample] = bounded;
    if (sample == 0U) {
        values[macro][intervals] = bounded;
    }
}

FLASHMEM bool MacroAutomationTakeState::sampleFixedColumn_(
    uint8_t macro,
    uint32_t elapsedTick,
    uint8_t value
) {
    if (macro >= VALUE_COLUMN_COUNT || sampleCount < 2U || durationTicks == 0U) {
        return false;
    }
    const uint16_t bit = static_cast<uint16_t>(1U << macro);
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint64_t absoluteTick =
        static_cast<uint64_t>(startProjectPhaseTick) + elapsedTick;
    const uint64_t currentOrdinal =
        (absoluteTick * intervals) / durationTicks;
    const uint8_t bounded = std::min<uint8_t>(value, 127U);
    if ((writeCursorMask & bit) == 0U) {
        writeFixedGridValue_(macro, currentOrdinal, bounded);
        lastWriteElapsedTicks[macro] = elapsedTick;
        lastWriteValues[macro] = bounded;
        writeCursorMask = static_cast<uint16_t>(writeCursorMask | bit);
        return true;
    }

    const uint32_t previousElapsed = lastWriteElapsedTicks[macro];
    if (elapsedTick < previousElapsed) return false;
    const uint64_t previousTick =
        static_cast<uint64_t>(startProjectPhaseTick) + previousElapsed;
    const uint64_t previousOrdinal =
        (previousTick * intervals) / durationTicks;
    uint64_t firstOrdinal = previousOrdinal;
    if (currentOrdinal > firstOrdinal + intervals) {
        // A clock jump can overwrite at most the most recent complete loop.
        firstOrdinal = currentOrdinal - intervals;
    }
    const uint32_t span = elapsedTick - previousElapsed;
    const int32_t valueDelta = static_cast<int32_t>(bounded) -
        lastWriteValues[macro];
    for (uint64_t ordinal = firstOrdinal + 1U;
         ordinal <= currentOrdinal;
         ++ordinal) {
        const uint64_t crossedAbsoluteTick =
            (ordinal * durationTicks + intervals - 1U) / intervals;
        const uint64_t crossedElapsed = crossedAbsoluteTick > startProjectPhaseTick
            ? crossedAbsoluteTick - startProjectPhaseTick
            : 0U;
        const uint32_t relative = span == 0U
            ? span
            : static_cast<uint32_t>(std::min<uint64_t>(
                  crossedElapsed > previousElapsed
                      ? crossedElapsed - previousElapsed
                      : 0U,
                  span
              ));
        const int32_t interpolated = span == 0U
            ? bounded
            : static_cast<int32_t>(lastWriteValues[macro]) +
                  (valueDelta * static_cast<int32_t>(relative) +
                   static_cast<int32_t>(span / 2U)) /
                      static_cast<int32_t>(span);
        writeFixedGridValue_(
            macro,
            ordinal,
            static_cast<uint8_t>(std::clamp<int32_t>(interpolated, 0, 127))
        );
    }
    writeFixedGridValue_(macro, currentOrdinal, bounded);
    lastWriteElapsedTicks[macro] = elapsedTick;
    lastWriteValues[macro] = bounded;
    return true;
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
