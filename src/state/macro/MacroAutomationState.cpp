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

FLASHMEM uint16_t reclaimableCurvePoints(const MacroAutomationCurveRef& ref,
                                         const MacroAutomationPointPool& pool) {
    return curveRangeValid(ref, pool) ? ref.pointCount : 0;
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
    target.durationTicks = durationTicks;
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

}  // namespace

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

    const uint16_t oldCount = reclaimableCurvePoints(slot.automation, bank.pointPool);
    const uint16_t freeCount =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - bank.pointPool.used);
    if (lane.pointCount > static_cast<uint16_t>(freeCount + oldCount)) return false;

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

    const uint16_t oldCount = reclaimableCurvePoints(slot.modulation, bank.pointPool);
    const uint16_t freeCount =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - bank.pointPool.used);
    if (shape.pointCount > static_cast<uint16_t>(freeCount + oldCount)) return false;

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
    macroAutomationCompactPool(bank);
}

FLASHMEM bool macroAutomationCopySlotState(MacroAutomationPointPool& destPool,
                                           MacroAutomationSlotState& dest,
                                           const MacroAutomationPointPool& sourcePool,
                                           const MacroAutomationSlotState& source) {
    MacroAutomationSlotState next{};
    next.modulationDepth = source.modulationDepth;
    const uint16_t required = static_cast<uint16_t>(
        reclaimableCurvePoints(source.automation, sourcePool) +
        reclaimableCurvePoints(source.modulation, sourcePool)
    );
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
    const uint16_t oldCount = static_cast<uint16_t>(
        reclaimableCurvePoints(dest.automation, destBank.pointPool) +
        reclaimableCurvePoints(dest.modulation, destBank.pointPool)
    );
    const uint16_t required = static_cast<uint16_t>(
        reclaimableCurvePoints(source.automation, sourcePool) +
        reclaimableCurvePoints(source.modulation, sourcePool)
    );
    const uint16_t freeCount =
        static_cast<uint16_t>(MACRO_AUTOMATION_POINT_POOL_CAPACITY - destBank.pointPool.used);
    if (required > static_cast<uint16_t>(freeCount + oldCount)) return false;

    dest = {};
    macroAutomationCompactPool(destBank);
    return macroAutomationCopySlotState(destBank.pointPool, dest, sourcePool, source);
}

}  // namespace core::state::macro
