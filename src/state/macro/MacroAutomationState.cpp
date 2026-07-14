#include "state/macro/MacroAutomationState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

namespace {

struct CurveDescriptor {
    MacroAutomationCurveRef* ref = nullptr;
    uint16_t offset = 0;
    uint16_t count = 0;
};

struct CurveRange {
    uint16_t start = 0;
    uint16_t end = 0;
};

constexpr uint32_t FNV_OFFSET_BASIS = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

FLASHMEM uint16_t clampedPointTick(float beat, uint16_t durationTicks) {
    if (!std::isfinite(beat) || beat <= 0.0f) return 0;
    const float ticks =
        beat * static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT);
    const int rounded = static_cast<int>(std::lround(ticks));
    return static_cast<uint16_t>(std::clamp(
        rounded,
        0,
        static_cast<int>(durationTicks)
    ));
}

FLASHMEM bool curveRangeValid(const MacroAutomationCurveRef& ref,
                              const MacroAutomationPointPool& pool) {
    if (!ref.active) return false;
    if (ref.pointCount == 0 || ref.pointOffset >= pool.used) return false;
    const uint32_t end =
        static_cast<uint32_t>(ref.pointOffset) + static_cast<uint32_t>(ref.pointCount);
    return end <= pool.used && end <= MACRO_AUTOMATION_POINT_POOL_CAPACITY;
}

FLASHMEM uint32_t fingerprintByte(uint32_t fingerprint, uint8_t value) {
    return (fingerprint ^ value) * FNV_PRIME;
}

FLASHMEM uint32_t fingerprintU16(uint32_t fingerprint, uint16_t value) {
    fingerprint = fingerprintByte(fingerprint, static_cast<uint8_t>(value & 0xFFU));
    return fingerprintByte(fingerprint, static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

FLASHMEM uint32_t curveFingerprint(const MacroAutomationCurveRef& curve,
                                   const MacroAutomationPointPool& pool) {
    uint32_t fingerprint = FNV_OFFSET_BASIS;
    fingerprint = fingerprintByte(fingerprint, curve.active ? 1U : 0U);
    fingerprint = fingerprintByte(
        fingerprint,
        static_cast<uint8_t>(curve.playbackState)
    );
    fingerprint = fingerprintU16(fingerprint, curve.pointCount);
    fingerprint = fingerprintU16(fingerprint, curve.sourceDurationTicks);
    fingerprint = fingerprintU16(fingerprint, curve.durationTicks);
    fingerprint = fingerprintU16(fingerprint, curve.windowOffsetTicks);
    fingerprint = fingerprintByte(
        fingerprint,
        static_cast<uint8_t>(curve.interpolation)
    );
    fingerprint = fingerprintByte(
        fingerprint,
        static_cast<uint8_t>(curve.modulationOrigin)
    );
    if (!curveRangeValid(curve, pool)) return fingerprint;
    for (uint16_t i = 0; i < curve.pointCount; ++i) {
        const auto& point = pool.points[static_cast<uint16_t>(curve.pointOffset + i)];
        fingerprint = fingerprintU16(fingerprint, point.tick);
        fingerprint = fingerprintU16(
            fingerprint,
            static_cast<uint16_t>(point.value)
        );
    }
    return fingerprint;
}

FLASHMEM uint32_t modulationTargetFingerprint(
    const MacroAutomationSlotState& slot,
    const MacroAutomationPointPool& pool
) {
    uint32_t fingerprint = curveFingerprint(slot.modulation, pool);
    uint32_t depthBits = 0;
    static_assert(sizeof(depthBits) == sizeof(slot.modulationDepth));
    std::memcpy(&depthBits, &slot.modulationDepth, sizeof(depthBits));
    fingerprint = fingerprintU16(fingerprint, static_cast<uint16_t>(depthBits));
    return fingerprintU16(fingerprint, static_cast<uint16_t>(depthBits >> 16U));
}

FLASHMEM bool curveValidForMutation(const MacroAutomationCurveRef& curve,
                                    const MacroAutomationPointPool& pool) {
    if (!macroCurvePlaybackStateValid(curve.playbackState) ||
        !macroModulationOriginValid(curve.modulationOrigin) ||
        curve.interpolation != MacroAutomationInterpolation::LINEAR) {
        return false;
    }
    if (!curve.active) return curve.pointCount == 0;
    if (!curveRangeValid(curve, pool)) return false;
    uint16_t previousTick = 0;
    for (uint16_t i = 0; i < curve.pointCount; ++i) {
        const auto& point = pool.points[static_cast<uint16_t>(curve.pointOffset + i)];
        if (point.tick > curve.sourceDurationTicks) return false;
        if (i > 0 && point.tick < previousTick) return false;
        previousTick = point.tick;
    }
    return true;
}

FLASHMEM bool bankValidForMutation(const MacroAutomationBankState& bank) {
    if (bank.entryCount > MACRO_AUTOMATION_SLOT_CAPACITY ||
        bank.pointPool.used > MACRO_AUTOMATION_POINT_POOL_CAPACITY) {
        return false;
    }

    std::array<CurveRange, MACRO_AUTOMATION_SLOT_CAPACITY * 2U> ranges{};
    uint8_t rangeCount = 0;
    for (uint8_t i = 0; i < bank.entryCount; ++i) {
        const auto& entry = bank.entries[i];
        if (!entry.active || !macroAutomationAddressValid(entry.address)) return false;
        for (uint8_t j = static_cast<uint8_t>(i + 1U); j < bank.entryCount; ++j) {
            if (bank.entries[j].active &&
                macroAutomationAddressEquals(entry.address, bank.entries[j].address)) {
                return false;
            }
        }
        if (!macroAutomationCurveLifecycleValid(entry.state.automation) ||
            !macroModulationCurveLifecycleValid(entry.state.modulation)) {
            return false;
        }
        const std::array<const MacroAutomationCurveRef*, 2> curves{
            &entry.state.automation,
            &entry.state.modulation,
        };
        for (const auto* curve : curves) {
            if (curve == nullptr || !curveValidForMutation(*curve, bank.pointPool)) {
                return false;
            }
            if (!curve->active) continue;
            if (rangeCount >= ranges.size()) return false;
            ranges[rangeCount++] = CurveRange{
                .start = curve->pointOffset,
                .end = static_cast<uint16_t>(curve->pointOffset + curve->pointCount),
            };
        }
    }

    for (uint8_t i = 1; i < rangeCount; ++i) {
        const CurveRange current = ranges[i];
        uint8_t j = i;
        while (j > 0 && ranges[j - 1U].start > current.start) {
            ranges[j] = ranges[j - 1U];
            --j;
        }
        ranges[j] = current;
    }
    uint16_t cursor = 0;
    for (uint8_t i = 0; i < rangeCount; ++i) {
        if (ranges[i].start != cursor || ranges[i].end <= ranges[i].start) return false;
        cursor = ranges[i].end;
    }
    return cursor == bank.pointPool.used;
}

FLASHMEM double curveIntegralToTick(
    const MacroAutomationCurveRef& automation,
    const MacroAutomationPointPool& pool,
    uint16_t targetTick
) {
    if (!curveRangeValid(automation, pool) || targetTick == 0U) return 0.0;
    const uint16_t count = automation.pointCount;
    const auto pointAt = [&](uint16_t index) -> const MacroPackedCurvePoint& {
        return pool.points[static_cast<uint16_t>(automation.pointOffset + index)];
    };
    const auto valueAt = [&](uint16_t index) {
        return static_cast<double>(
            macroAutomationUnpackValue(pointAt(index).value, false)
        );
    };

    const auto& first = pointAt(0);
    const double firstValue = valueAt(0);
    if (targetTick <= first.tick) {
        return firstValue * static_cast<double>(targetTick);
    }
    double integral = firstValue * static_cast<double>(first.tick);
    for (uint16_t i = 1; i < count; ++i) {
        const auto& previous = pointAt(static_cast<uint16_t>(i - 1U));
        const auto& current = pointAt(i);
        const double previousValue = valueAt(static_cast<uint16_t>(i - 1U));
        const double currentValue = valueAt(i);
        if (targetTick <= current.tick) {
            const uint16_t elapsed = static_cast<uint16_t>(
                targetTick - previous.tick
            );
            const uint16_t span = static_cast<uint16_t>(
                current.tick - previous.tick
            );
            const double endValue = span == 0U
                ? currentValue
                : previousValue + (currentValue - previousValue) *
                    (static_cast<double>(elapsed) / static_cast<double>(span));
            return integral +
                (previousValue + endValue) * 0.5 *
                    static_cast<double>(elapsed);
        }
        integral += (previousValue + currentValue) * 0.5 *
            static_cast<double>(current.tick - previous.tick);
    }

    const auto& last = pointAt(static_cast<uint16_t>(count - 1U));
    if (targetTick > last.tick) {
        integral += valueAt(static_cast<uint16_t>(count - 1U)) *
            static_cast<double>(targetTick - last.tick);
    }
    return integral;
}

FLASHMEM float timeWeightedCurveMean(
    const MacroAutomationCurveRef& automation,
    const MacroAutomationPointPool& pool
) {
    if (!curveRangeValid(automation, pool)) return 0.0f;
    const auto& last = pool.points[static_cast<uint16_t>(
        automation.pointOffset + automation.pointCount - 1U
    )];
    const uint16_t sourceTicks = std::max<uint16_t>({
        automation.sourceDurationTicks,
        last.tick,
        1U,
    });
    const uint16_t durationTicks = automation.durationTicks == 0U
        ? MACRO_AUTOMATION_TICKS_PER_BEAT
        : automation.durationTicks;
    const uint16_t offset = static_cast<uint16_t>(
        automation.windowOffsetTicks % sourceTicks
    );
    const double cycleIntegral = curveIntegralToTick(
        automation,
        pool,
        sourceTicks
    );
    const uint32_t fullCycles = durationTicks / sourceTicks;
    uint16_t remainder = static_cast<uint16_t>(durationTicks % sourceTicks);
    double integral = cycleIntegral * static_cast<double>(fullCycles);
    if (remainder > 0U) {
        const uint16_t firstLength = std::min<uint16_t>(
            remainder,
            static_cast<uint16_t>(sourceTicks - offset)
        );
        integral += curveIntegralToTick(
            automation,
            pool,
            static_cast<uint16_t>(offset + firstLength)
        ) - curveIntegralToTick(automation, pool, offset);
        remainder = static_cast<uint16_t>(remainder - firstLength);
        if (remainder > 0U) {
            integral += curveIntegralToTick(automation, pool, remainder);
        }
    }
    return macroAutomationClamp01(static_cast<float>(
        integral / static_cast<double>(durationTicks)
    ));
}

FLASHMEM float conversionReference(const MacroAutomationCurveRef& automation,
                                   const MacroAutomationPointPool& pool,
                                   MacroAutomationConversionPolicy policy) {
    if (!curveRangeValid(automation, pool)) return 0.0f;
    const uint16_t count = automation.pointCount;
    if (policy == MacroAutomationConversionPolicy::FIRST) {
        return macroAutomationUnpackValue(pool.points[automation.pointOffset].value, false);
    }
    if (policy == MacroAutomationConversionPolicy::MIN) {
        int16_t minimum = 32767;
        for (uint16_t i = 0; i < count; ++i) {
            minimum = std::min(
                minimum,
                pool.points[static_cast<uint16_t>(automation.pointOffset + i)].value
            );
        }
        return macroAutomationUnpackValue(minimum, false);
    }

    return timeWeightedCurveMean(automation, pool);
}

FLASHMEM float conversionAmplitude(
    const MacroAutomationCurveRef& automation,
    const MacroAutomationPointPool& pool,
    float reference
) {
    if (!curveRangeValid(automation, pool)) return 0.0f;
    float amplitude = 0.0f;
    for (uint16_t i = 0; i < automation.pointCount; ++i) {
        const float value = macroAutomationUnpackValue(
            pool.points[static_cast<uint16_t>(automation.pointOffset + i)].value,
            false
        );
        amplitude = std::max(amplitude, std::fabs(value - reference));
    }
    return macroAutomationClamp01(amplitude);
}

FLASHMEM bool conversionPolicyValid(MacroAutomationConversionPolicy policy) {
    switch (policy) {
        case MacroAutomationConversionPolicy::MEAN:
        case MacroAutomationConversionPolicy::FIRST:
        case MacroAutomationConversionPolicy::MIN:
            return true;
        default:
            return false;
    }
}

FLASHMEM bool appendPackedCurve(MacroAutomationPointPool& pool,
                                MacroAutomationCurveRef& target,
                                const MacroCurvePoint* source,
                                uint16_t sourceCount,
                                float durationBeats,
                                MacroAutomationInterpolation interpolation,
                                bool signedValues) {
    target = {};
    if (source == nullptr || sourceCount == 0) return true;
    const uint16_t capacityLeft =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - pool.used);
    if (sourceCount > capacityLeft) return false;

    const uint16_t durationTicks = macroAutomationTicksFromBeats(durationBeats);
    const uint16_t start = pool.used;
    uint16_t written = 0;
    for (uint16_t i = 0; i < sourceCount; ++i) {
        const uint16_t tick = clampedPointTick(source[i].beat, durationTicks);
        const int16_t value = macroAutomationPackValue(source[i].value, signedValues);
        if (written > 0 &&
            pool.points[static_cast<uint16_t>(start + written - 1U)].tick == tick) {
            pool.points[static_cast<uint16_t>(start + written - 1U)].value = value;
            continue;
        }
        pool.points[static_cast<uint16_t>(start + written)] = MacroPackedCurvePoint{
            .tick = tick,
            .value = value,
        };
        ++written;
    }

    if (written == 0) return true;
    pool.used = static_cast<uint16_t>(pool.used + written);
    target.active = true;
    target.pointOffset = start;
    target.pointCount = written;
    target.sourceDurationTicks = durationTicks;
    target.durationTicks = durationTicks;
    target.windowOffsetTicks = 0;
    target.interpolation = interpolation;
    return true;
}

FLASHMEM bool copyCurve(MacroAutomationPointPool& destPool,
                        MacroAutomationCurveRef& dest,
                        const MacroAutomationPointPool& sourcePool,
                        const MacroAutomationCurveRef& source) {
    dest = {};
    if (!curveRangeValid(source, sourcePool)) return true;
    const uint16_t capacityLeft =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - destPool.used);
    if (source.pointCount > capacityLeft) return false;
    const uint16_t start = destPool.used;
    std::memcpy(
        &destPool.points[start],
        &sourcePool.points[source.pointOffset],
        static_cast<size_t>(source.pointCount) * sizeof(MacroPackedCurvePoint)
    );
    destPool.used = static_cast<uint16_t>(destPool.used + source.pointCount);
    dest = source;
    dest.pointOffset = start;
    return true;
}

FLASHMEM void addCurveDescriptor(std::array<CurveDescriptor, MACRO_AUTOMATION_SLOT_CAPACITY * 2>& curves,
                                 uint8_t& count,
                                 MacroAutomationCurveRef& ref,
                                 MacroAutomationPointPool& pool) {
    if (!curveRangeValid(ref, pool)) {
        ref = {};
        return;
    }
    if (count >= curves.size()) {
        ref = {};
        return;
    }
    curves[count++] = CurveDescriptor{&ref, ref.pointOffset, ref.pointCount};
}

FLASHMEM void sortCurvesByOffset(
    std::array<CurveDescriptor, MACRO_AUTOMATION_SLOT_CAPACITY * 2>& curves,
    uint8_t count
) {
    for (uint8_t i = 1; i < count; ++i) {
        CurveDescriptor current = curves[i];
        uint8_t j = i;
        while (j > 0 && curves[j - 1U].offset > current.offset) {
            curves[j] = curves[j - 1U];
            --j;
        }
        curves[j] = current;
    }
}

enum class ClearScope : uint8_t {
    PAGE,
    TRACK,
};

FLASHMEM bool addressMatchesClearScope(const MacroAutomationSlotAddress& address,
                                       ClearScope scope,
                                       uint8_t track,
                                       uint8_t page) {
    if (address.track != track) return false;
    if (scope == ClearScope::TRACK) return true;
    return address.page == page;
}

FLASHMEM bool clearSlotsInScope(MacroAutomationBankState& bank,
                                ClearScope scope,
                                uint8_t track,
                                uint8_t page) {
    if (track >= TRACK_COUNT) return false;
    if (scope == ClearScope::PAGE && page >= PAGE_COUNT) return false;

    const uint8_t count = bank.entryCount > MACRO_AUTOMATION_SLOT_CAPACITY
        ? MACRO_AUTOMATION_SLOT_CAPACITY
        : bank.entryCount;
    uint8_t write = 0;
    bool changed = bank.entryCount != count;
    for (uint8_t read = 0; read < count; ++read) {
        const auto& entry = bank.entries[read];
        if (!entry.active) {
            changed = true;
            continue;
        }
        if (addressMatchesClearScope(entry.address, scope, track, page)) {
            changed = true;
            continue;
        }
        if (write != read) {
            bank.entries[write] = entry;
        }
        ++write;
    }

    if (!changed) return false;
    for (uint8_t i = write; i < count; ++i) {
        bank.entries[i] = {};
    }
    bank.entryCount = write;
    macroAutomationCompactPool(bank);
    return true;
}

}  // namespace

FLASHMEM uint16_t macroAutomationStoredPointCount(
    const MacroAutomationCurveRef& curve,
    const MacroAutomationPointPool& pool
) {
    return curveRangeValid(curve, pool) ? curve.pointCount : 0;
}

FLASHMEM uint16_t macroAutomationStoredPointCount(
    const MacroAutomationSlotState& state,
    const MacroAutomationPointPool& pool
) {
    return static_cast<uint16_t>(
        macroAutomationStoredPointCount(state.automation, pool) +
        macroAutomationStoredPointCount(state.modulation, pool)
    );
}

FLASHMEM bool macroAutomationSlotStateValidForMutation(
    const MacroAutomationSlotState& state,
    const MacroAutomationPointPool& pool
) {
    return curveValidForMutation(state.automation, pool) &&
           curveValidForMutation(state.modulation, pool) &&
           macroAutomationCurveLifecycleValid(state.automation) &&
           macroModulationCurveLifecycleValid(state.modulation) &&
           std::isfinite(state.modulationDepth);
}

FLASHMEM void MacroAutomationBankState::clear() {
    entryCount = 0;
    entries = {};
    pointPool = {};
}

FLASHMEM bool macroAutomationAddressValid(const MacroAutomationSlotAddress& address) {
    return address.track < TRACK_COUNT &&
           address.page < PAGE_COUNT &&
           address.macro < MACRO_COUNT;
}

FLASHMEM bool macroAutomationAddressEquals(const MacroAutomationSlotAddress& lhs,
                                           const MacroAutomationSlotAddress& rhs) {
    return lhs.track == rhs.track &&
           lhs.page == rhs.page &&
           lhs.macro == rhs.macro;
}

FLASHMEM const MacroAutomationSlotState* macroAutomationFindSlot(
    const MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
) {
    if (!macroAutomationAddressValid(address)) return nullptr;
    const uint8_t count = bank.entryCount > MACRO_AUTOMATION_SLOT_CAPACITY
        ? MACRO_AUTOMATION_SLOT_CAPACITY
        : bank.entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = bank.entries[i];
        if (entry.active && macroAutomationAddressEquals(entry.address, address)) {
            return &entry.state;
        }
    }
    return nullptr;
}

FLASHMEM MacroAutomationSlotState* macroAutomationFindMutableSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
) {
    return const_cast<MacroAutomationSlotState*>(
        macroAutomationFindSlot(static_cast<const MacroAutomationBankState&>(bank), address)
    );
}

FLASHMEM MacroAutomationSlotState* macroAutomationGetOrCreateSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
) {
    if (!macroAutomationAddressValid(address)) return nullptr;
    if (auto* existing = macroAutomationFindMutableSlot(bank, address)) {
        return existing;
    }
    if (bank.entryCount >= MACRO_AUTOMATION_SLOT_CAPACITY) return nullptr;
    const uint8_t index = bank.entryCount;
    bank.entries[index] = MacroAutomationSlotEntry{
        .active = true,
        .address = address,
        .state = {},
    };
    bank.entryCount = static_cast<uint8_t>(bank.entryCount + 1U);
    return &bank.entries[index].state;
}

FLASHMEM bool macroAutomationClearSlot(MacroAutomationBankState& bank,
                                       const MacroAutomationSlotAddress& address) {
    if (!macroAutomationAddressValid(address)) return false;
    const uint8_t count = bank.entryCount > MACRO_AUTOMATION_SLOT_CAPACITY
        ? MACRO_AUTOMATION_SLOT_CAPACITY
        : bank.entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        if (!bank.entries[i].active ||
            !macroAutomationAddressEquals(bank.entries[i].address, address)) {
            continue;
        }
        for (uint8_t j = i; j + 1U < count; ++j) {
            bank.entries[j] = bank.entries[j + 1U];
        }
        bank.entries[count - 1U] = {};
        bank.entryCount = static_cast<uint8_t>(count - 1U);
        macroAutomationCompactPool(bank);
        return true;
    }
    return false;
}

FLASHMEM bool macroAutomationClearPage(MacroAutomationBankState& bank,
                                       uint8_t track,
                                       uint8_t page) {
    return clearSlotsInScope(bank, ClearScope::PAGE, track, page);
}

FLASHMEM bool macroAutomationClearTrack(MacroAutomationBankState& bank,
                                        uint8_t track) {
    return clearSlotsInScope(bank, ClearScope::TRACK, track, 0);
}

FLASHMEM bool macroAutomationSlotHasContent(const MacroAutomationSlotState& state) {
    return state.automation.active ||
           state.modulation.active ||
           state.modulationDepth != 0.0f;
}

FLASHMEM void macroAutomationCompactPool(MacroAutomationBankState& bank) {
    std::array<CurveDescriptor, MACRO_AUTOMATION_SLOT_CAPACITY * 2> curves{};
    uint8_t curveCount = 0;
    const uint8_t count = bank.entryCount > MACRO_AUTOMATION_SLOT_CAPACITY
        ? MACRO_AUTOMATION_SLOT_CAPACITY
        : bank.entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        if (!bank.entries[i].active) continue;
        addCurveDescriptor(curves, curveCount, bank.entries[i].state.automation, bank.pointPool);
        addCurveDescriptor(curves, curveCount, bank.entries[i].state.modulation, bank.pointPool);
    }

    sortCurvesByOffset(curves, curveCount);
    uint16_t cursor = 0;
    for (uint8_t i = 0; i < curveCount; ++i) {
        auto* ref = curves[i].ref;
        if (ref == nullptr || curves[i].count == 0) continue;
        if (cursor != curves[i].offset) {
            std::memmove(
                &bank.pointPool.points[cursor],
                &bank.pointPool.points[curves[i].offset],
                static_cast<size_t>(curves[i].count) * sizeof(MacroPackedCurvePoint)
            );
        }
        ref->pointOffset = cursor;
        cursor = static_cast<uint16_t>(cursor + curves[i].count);
    }
    bank.pointPool.used = cursor;
}

FLASHMEM bool macroAutomationAssignAutomation(MacroAutomationBankState& bank,
                                              MacroAutomationSlotState& slot,
                                              const MacroAutomationLane& lane) {
    if (!lane.active || lane.pointCount == 0) {
        macroAutomationClearAutomation(bank, slot);
        return true;
    }

    const uint16_t oldCount = macroAutomationStoredPointCount(slot.automation, bank.pointPool);
    const uint16_t freeCount =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - bank.pointPool.used);
    if (static_cast<uint32_t>(lane.pointCount) >
        static_cast<uint32_t>(freeCount) + static_cast<uint32_t>(oldCount)) {
        return false;
    }

    slot.automation = {};
    macroAutomationCompactPool(bank);
    return appendPackedCurve(
        bank.pointPool,
        slot.automation,
        lane.points.data(),
        lane.pointCount,
        lane.durationBeats,
        lane.interpolation,
        false
    );
}

FLASHMEM bool macroAutomationAssignModulation(MacroAutomationBankState& bank,
                                              MacroAutomationSlotState& slot,
                                              const MacroModulationShape& shape) {
    if (!shape.active || shape.pointCount == 0) {
        macroAutomationClearModulation(bank, slot);
        return true;
    }

    const uint16_t oldCount = macroAutomationStoredPointCount(slot.modulation, bank.pointPool);
    const uint16_t freeCount =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - bank.pointPool.used);
    if (static_cast<uint32_t>(shape.pointCount) >
        static_cast<uint32_t>(freeCount) + static_cast<uint32_t>(oldCount)) {
        return false;
    }

    slot.modulation = {};
    macroAutomationCompactPool(bank);
    return appendPackedCurve(
        bank.pointPool,
        slot.modulation,
        shape.points.data(),
        shape.pointCount,
        shape.durationBeats,
        shape.interpolation,
        true
    );
}

FLASHMEM void macroAutomationClearAutomation(MacroAutomationBankState& bank,
                                             MacroAutomationSlotState& slot) {
    slot.automation = {};
    macroAutomationCompactPool(bank);
}

FLASHMEM void macroAutomationClearModulation(MacroAutomationBankState& bank,
                                             MacroAutomationSlotState& slot) {
    slot.modulation = {};
    slot.modulationDepth = 0.0f;
    macroAutomationCompactPool(bank);
}

FLASHMEM MacroAutomationConversionPlan macroAutomationPreflightConversion(
    const MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address,
    MacroAutomationConversionPolicy policy,
    float currentStaticBase
) {
    MacroAutomationConversionPlan plan{};
    plan.address = address;
    plan.policy = policy;
    plan.expectedStaticBase = macroAutomationClamp01(currentStaticBase);
    if (!macroAutomationAddressValid(address) || !conversionPolicyValid(policy)) {
        plan.status = MacroAutomationConversionStatus::INVALID_ADDRESS;
        return plan;
    }
    if (!bankValidForMutation(bank)) {
        plan.status = MacroAutomationConversionStatus::INVALID_BANK;
        return plan;
    }

    const auto* slot = macroAutomationFindSlot(bank, address);
    if (slot == nullptr || !macroCurveStored(slot->automation)) {
        plan.status = MacroAutomationConversionStatus::NO_AUTOMATION;
        return plan;
    }

    plan.pointCount = slot->automation.pointCount;
    plan.reclaimablePointCount =
        macroAutomationStoredPointCount(slot->modulation, bank.pointPool);
    plan.freePointCount = static_cast<uint16_t>(
        MACRO_AUTOMATION_POINT_POOL_CAPACITY - bank.pointPool.used
    );
    plan.overwritesModulation = macroCurveStored(slot->modulation);
    plan.reference = conversionReference(slot->automation, bank.pointPool, policy);
    plan.normalizationAmplitude = conversionAmplitude(
        slot->automation,
        bank.pointPool,
        plan.reference
    );
    plan.sourceFingerprint = curveFingerprint(slot->automation, bank.pointPool);
    plan.targetFingerprint = modulationTargetFingerprint(*slot, bank.pointPool);

    if (static_cast<uint32_t>(plan.pointCount) >
        static_cast<uint32_t>(plan.freePointCount) +
            static_cast<uint32_t>(plan.reclaimablePointCount)) {
        plan.status = MacroAutomationConversionStatus::POINT_POOL_EXHAUSTED;
        return plan;
    }
    plan.status = plan.overwritesModulation
        ? MacroAutomationConversionStatus::OVERWRITE_REQUIRED
        : MacroAutomationConversionStatus::READY;
    return plan;
}

FLASHMEM bool macroAutomationApplyConversion(
    MacroAutomationBankState& bank,
    float& staticBase,
    const MacroAutomationConversionPlan& plan,
    bool overwriteConfirmed
) {
    if (!plan.actionable()) return false;
    const float currentBase = macroAutomationClamp01(staticBase);
    if (std::fabs(currentBase - plan.expectedStaticBase) > 0.000001f) return false;

    const auto current = macroAutomationPreflightConversion(
        bank,
        plan.address,
        plan.policy,
        currentBase
    );
    if (!current.actionable() ||
        current.sourceFingerprint != plan.sourceFingerprint ||
        current.targetFingerprint != plan.targetFingerprint ||
        current.pointCount != plan.pointCount ||
        current.overwritesModulation != plan.overwritesModulation ||
        std::fabs(current.reference - plan.reference) > 0.000001f ||
        std::fabs(
            current.normalizationAmplitude - plan.normalizationAmplitude
        ) > 0.000001f) {
        return false;
    }
    if (current.overwritesModulation && !overwriteConfirmed) return false;

    auto* slot = macroAutomationFindMutableSlot(bank, plan.address);
    if (slot == nullptr) return false;

    // From this point the validated preflight makes every remaining step
    // infallible: reclaim first, then append exactly `pointCount` points.
    slot->modulation = {};
    macroAutomationCompactPool(bank);
    const MacroAutomationCurveRef source = slot->automation;
    const uint16_t start = bank.pointPool.used;
    for (uint16_t i = 0; i < source.pointCount; ++i) {
        const auto& sourcePoint =
            bank.pointPool.points[static_cast<uint16_t>(source.pointOffset + i)];
        const float absolute = macroAutomationUnpackValue(sourcePoint.value, false);
        const float normalized = current.normalizationAmplitude > 0.000001f
            ? (absolute - current.reference) / current.normalizationAmplitude
            : 0.0f;
        bank.pointPool.points[static_cast<uint16_t>(start + i)] = MacroPackedCurvePoint{
            .tick = sourcePoint.tick,
            .value = macroAutomationPackValue(normalized, true),
        };
    }
    bank.pointPool.used = static_cast<uint16_t>(start + source.pointCount);

    slot->modulation = source;
    slot->modulation.pointOffset = start;
    slot->modulation.playbackState = MacroCurvePlaybackState::ACTIVE;
    slot->modulation.modulationOrigin = macroModulationOriginForConversion(plan.policy);
    slot->modulationDepth = current.normalizationAmplitude;
    slot->automation.playbackState = MacroCurvePlaybackState::OFF;
    staticBase = current.reference;
    return true;
}

FLASHMEM bool macroAutomationCopySlotState(MacroAutomationPointPool& destPool,
                                           MacroAutomationSlotState& dest,
                                           const MacroAutomationPointPool& sourcePool,
                                           const MacroAutomationSlotState& source) {
    if (!macroAutomationSlotStateValidForMutation(source, sourcePool)) {
        return false;
    }
    MacroAutomationSlotState next{};
    next.modulationDepth = macroAutomationClamp01(source.modulationDepth);
    const uint16_t required = macroAutomationStoredPointCount(source, sourcePool);
    const uint16_t freeCount =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - destPool.used);
    if (required > freeCount) return false;
    if (!copyCurve(destPool, next.automation, sourcePool, source.automation)) return false;
    if (!copyCurve(destPool, next.modulation, sourcePool, source.modulation)) return false;
    dest = next;
    return true;
}

FLASHMEM bool macroAutomationCopySlotState(MacroAutomationBankState& destBank,
                                           MacroAutomationSlotState& dest,
                                           const MacroAutomationPointPool& sourcePool,
                                           const MacroAutomationSlotState& source) {
    // Source admission must be complete before clearing `dest`: compaction
    // rewrites offsets throughout the bank and cannot be rolled back locally.
    if (!macroAutomationSlotStateValidForMutation(source, sourcePool)) {
        return false;
    }
    const uint16_t oldCount = macroAutomationStoredPointCount(dest, destBank.pointPool);
    const uint16_t required = macroAutomationStoredPointCount(source, sourcePool);
    const uint16_t freeCount =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - destBank.pointPool.used);
    if (static_cast<uint32_t>(required) >
        static_cast<uint32_t>(freeCount) + static_cast<uint32_t>(oldCount)) {
        return false;
    }

    dest = {};
    macroAutomationCompactPool(destBank);
    return macroAutomationCopySlotState(destBank.pointPool, dest, sourcePool, source);
}

FLASHMEM bool macroAutomationCopyAutomationState(
    MacroAutomationBankState& destBank,
    MacroAutomationSlotState& dest,
    const MacroAutomationPointPool& sourcePool,
    const MacroAutomationSlotState& source
) {
    if (!curveValidForMutation(source.automation, sourcePool) ||
        !macroCurveStored(source.automation)) {
        return false;
    }

    const uint16_t oldCount = macroAutomationStoredPointCount(
        dest.automation,
        destBank.pointPool
    );
    const uint16_t required = source.automation.pointCount;
    const uint16_t freeCount = static_cast<uint16_t>(
        MACRO_AUTOMATION_POINT_POOL_CAPACITY - destBank.pointPool.used
    );
    if (static_cast<uint32_t>(required) >
        static_cast<uint32_t>(freeCount) + oldCount) {
        return false;
    }

    dest.automation = {};
    macroAutomationCompactPool(destBank);
    MacroAutomationCurveRef next{};
    if (!copyCurve(destBank.pointPool, next, sourcePool, source.automation)) {
        return false;
    }
    dest.automation = next;
    return true;
}

FLASHMEM bool macroAutomationCopyModulationState(
    MacroAutomationBankState& destBank,
    MacroAutomationSlotState& dest,
    const MacroAutomationPointPool& sourcePool,
    const MacroAutomationSlotState& source
) {
    if (!curveValidForMutation(source.modulation, sourcePool) ||
        !macroModulationCurveLifecycleValid(source.modulation) ||
        !macroCurveStored(source.modulation) ||
        !std::isfinite(source.modulationDepth)) {
        return false;
    }

    const uint16_t oldCount = macroAutomationStoredPointCount(
        dest.modulation,
        destBank.pointPool
    );
    const uint16_t required = source.modulation.pointCount;
    const uint16_t freeCount = static_cast<uint16_t>(
        MACRO_AUTOMATION_POINT_POOL_CAPACITY - destBank.pointPool.used
    );
    if (static_cast<uint32_t>(required) >
        static_cast<uint32_t>(freeCount) + oldCount) {
        return false;
    }

    // Validation and capacity admission are complete before the first write;
    // the single curve copy below is therefore infallible.
    dest.modulation = {};
    macroAutomationCompactPool(destBank);
    MacroAutomationCurveRef next{};
    if (!copyCurve(destBank.pointPool, next, sourcePool, source.modulation)) {
        return false;
    }
    dest.modulation = next;
    dest.modulationDepth = macroAutomationClamp01(source.modulationDepth);
    return true;
}

}  // namespace core::state::macro
