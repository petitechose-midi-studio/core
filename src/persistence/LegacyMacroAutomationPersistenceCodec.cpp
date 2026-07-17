#include "persistence/LegacyMacroAutomationPersistenceCodec.hpp"

#include <array>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"

namespace core::persistence::macro_automation_legacy_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace macro = core::state::macro;

struct CurveRange {
    uint16_t start = 0;
    uint16_t end = 0;
};

FLASHMEM bool writeCurveRef(
    binary::Writer& writer,
    const macro::MacroAutomationCurveRef& curve
) {
    return writer.writeU8(curve.active ? 1U : 0U) &&
           writer.writeU8(static_cast<uint8_t>(curve.playbackState)) &&
           writer.writeU16(curve.pointOffset) &&
           writer.writeU16(curve.pointCount) &&
           writer.writeU16(curve.sourceDurationTicks) &&
           writer.writeU16(curve.durationTicks) &&
           writer.writeU16(curve.windowOffsetTicks) &&
           writer.writeU8(static_cast<uint8_t>(curve.interpolation)) &&
           writer.writeU8(static_cast<uint8_t>(curve.modulationOrigin));
}

FLASHMEM bool readCurveRef(
    binary::Reader& reader,
    macro::MacroAutomationCurveRef& curve,
    bool v14
) {
    uint8_t active = 0;
    uint8_t playbackState = 0;
    uint8_t interpolation = 0;
    uint8_t modulationOrigin = 0;
    if (!reader.readU8(active) ||
        !reader.readU8(playbackState) ||
        !reader.readU16(curve.pointOffset) ||
        !reader.readU16(curve.pointCount) ||
        !reader.readU16(curve.sourceDurationTicks) ||
        !reader.readU16(curve.durationTicks) ||
        !reader.readU16(curve.windowOffsetTicks) ||
        !reader.readU8(interpolation) ||
        !reader.readU8(modulationOrigin)) {
        return false;
    }
    if (active > 1U ||
        interpolation !=
            static_cast<uint8_t>(macro::MacroAutomationInterpolation::LINEAR) ||
        (v14 && (playbackState != 0U || modulationOrigin != 0U))) {
        return false;
    }
    curve.active = active != 0U;
    curve.interpolation = macro::MacroAutomationInterpolation::LINEAR;
    curve.playbackState = v14
        ? macro::MacroCurvePlaybackState::ACTIVE
        : static_cast<macro::MacroCurvePlaybackState>(playbackState);
    curve.modulationOrigin = v14
        ? macro::MacroModulationOrigin::NATIVE
        : static_cast<macro::MacroModulationOrigin>(modulationOrigin);
    return macro::macroCurvePlaybackStateValid(curve.playbackState) &&
           macro::macroModulationOriginValid(curve.modulationOrigin);
}

FLASHMEM bool writeSlotState(
    binary::Writer& writer,
    const macro::MacroAutomationSlotState& state
) {
    return writeCurveRef(writer, state.automation) &&
           writeCurveRef(writer, state.modulation) &&
           writer.writeFloat32(state.modulationDepth);
}

FLASHMEM void normalizeLegacyPlayback(
    macro::MacroAutomationSlotState& state
) {
    // SUSPENDED_AFTER_RECORD belonged to the former coupled-source lifecycle.
    // A stored Modulation loop is now interrupted only by an explicit user
    // action, so every migration boundary upgrades this value to ACTIVE.
    if (state.modulation.playbackState ==
        macro::MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD) {
        state.modulation.playbackState = macro::MacroCurvePlaybackState::ACTIVE;
    }
}

FLASHMEM bool readSlotState(
    binary::Reader& reader,
    macro::MacroAutomationSlotState& state,
    bool v14
) {
    if (!readCurveRef(reader, state.automation, v14) ||
        !readCurveRef(reader, state.modulation, v14) ||
        !reader.readFloat32(state.modulationDepth)) {
        return false;
    }
    normalizeLegacyPlayback(state);
    return true;
}

FLASHMEM bool validCurve(
    const macro::MacroAutomationCurveRef& curve,
    const macro::MacroAutomationPointPool& pool
) {
    if (!macro::macroCurvePlaybackStateValid(curve.playbackState) ||
        !macro::macroModulationOriginValid(curve.modulationOrigin) ||
        curve.interpolation != macro::MacroAutomationInterpolation::LINEAR) {
        return false;
    }
    if (!curve.active) return curve.pointCount == 0U;
    if (curve.durationTicks == 0U || curve.sourceDurationTicks == 0U ||
        curve.pointCount == 0U ||
        curve.windowOffsetTicks > curve.sourceDurationTicks ||
        curve.pointOffset >= pool.used) {
        return false;
    }
    const uint32_t end =
        static_cast<uint32_t>(curve.pointOffset) + curve.pointCount;
    if (end > pool.used || end > macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) {
        return false;
    }
    uint16_t previousTick = 0;
    for (uint16_t index = 0; index < curve.pointCount; ++index) {
        const auto& point = pool.points[
            static_cast<uint16_t>(curve.pointOffset + index)
        ];
        if (point.tick > curve.sourceDurationTicks ||
            (index > 0U && point.tick < previousTick)) {
            return false;
        }
        previousTick = point.tick;
    }
    return true;
}

FLASHMEM bool appendRange(
    const macro::MacroAutomationCurveRef& curve,
    std::array<CurveRange, macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2U>& ranges,
    uint8_t& count
) {
    if (!curve.active) return true;
    if (count >= ranges.size()) return false;
    ranges[count++] = {
        curve.pointOffset,
        static_cast<uint16_t>(curve.pointOffset + curve.pointCount),
    };
    return true;
}

FLASHMEM void sortRanges(
    std::array<CurveRange, macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2U>& ranges,
    uint8_t count
) {
    for (uint8_t index = 1U; index < count; ++index) {
        const CurveRange current = ranges[index];
        uint8_t cursor = index;
        while (cursor > 0U && ranges[cursor - 1U].start > current.start) {
            ranges[cursor] = ranges[cursor - 1U];
            --cursor;
        }
        ranges[cursor] = current;
    }
}

FLASHMEM bool fullyCovered(
    const std::array<CurveRange, macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2U>& ranges,
    uint8_t count,
    uint16_t poolUsed
) {
    if (poolUsed == 0U) return count == 0U;
    if (count == 0U || ranges[0].start != 0U) return false;
    uint16_t cursor = 0;
    for (uint8_t index = 0; index < count; ++index) {
        if (ranges[index].start != cursor ||
            ranges[index].end <= ranges[index].start) {
            return false;
        }
        cursor = ranges[index].end;
    }
    return cursor == poolUsed;
}

}  // namespace

FLASHMEM uint32_t payloadSize(uint8_t entryCount, uint16_t pointCount) {
    return HEADER_SIZE + static_cast<uint32_t>(entryCount) * ENTRY_SIZE +
           static_cast<uint32_t>(pointCount) * POINT_SIZE;
}

FLASHMEM bool validBank(const macro::MacroAutomationBankState& bank) {
    if (bank.entryCount > macro::MACRO_AUTOMATION_SLOT_CAPACITY ||
        bank.pointPool.used > macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) {
        return false;
    }

    std::array<
        CurveRange,
        macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2U
    > ranges{};
    uint8_t rangeCount = 0;
    for (uint8_t index = 0; index < bank.entryCount; ++index) {
        const auto& entry = bank.entries[index];
        if (!entry.active || !macro::macroAutomationAddressValid(entry.address)) {
            return false;
        }
        for (uint8_t prior = 0; prior < index; ++prior) {
            if (macro::macroAutomationAddressEquals(
                    bank.entries[prior].address,
                    entry.address
                )) {
                return false;
            }
        }
        if (!macro::macroAutomationCurveLifecycleValid(entry.state.automation) ||
            !macro::macroModulationCurveLifecycleValid(entry.state.modulation) ||
            !validCurve(entry.state.automation, bank.pointPool) ||
            !validCurve(entry.state.modulation, bank.pointPool) ||
            !std::isfinite(entry.state.modulationDepth) ||
            entry.state.modulationDepth < 0.0f ||
            entry.state.modulationDepth > 1.0f ||
            !appendRange(entry.state.automation, ranges, rangeCount) ||
            !appendRange(entry.state.modulation, ranges, rangeCount)) {
            return false;
        }
    }
    sortRanges(ranges, rangeCount);
    return fullyCovered(ranges, rangeCount, bank.pointPool.used);
}

FLASHMEM bool encodeV15(
    const macro::MacroAutomationBankState& bank,
    uint8_t* out,
    uint32_t outCapacity,
    uint32_t& outSize
) {
    outSize = 0U;
    if (out == nullptr || !validBank(bank)) return false;
    const uint32_t required = payloadSize(bank.entryCount, bank.pointPool.used);
    if (required > outCapacity || required > MAX_PAYLOAD_SIZE) return false;

    binary::Writer writer(out, outCapacity);
    if (!writer.writeU8(bank.entryCount) ||
        !writer.writeU8(0U) ||
        !writer.writeU16(bank.pointPool.used) ||
        !writer.writeU32(0U)) {
        return false;
    }
    for (uint8_t index = 0; index < bank.entryCount; ++index) {
        const auto& entry = bank.entries[index];
        if (!writer.writeU8(entry.address.track) ||
            !writer.writeU8(entry.address.page) ||
            !writer.writeU8(entry.address.macro) ||
            !writer.writeU8(0U) ||
            !writeSlotState(writer, entry.state)) {
            return false;
        }
    }
    for (uint16_t index = 0; index < bank.pointPool.used; ++index) {
        const auto& point = bank.pointPool.points[index];
        if (!writer.writeU16(point.tick) || !writer.writeI16(point.value)) {
            return false;
        }
    }
    if (!writer.ok() || writer.offset() != required) return false;
    outSize = required;
    return true;
}

FLASHMEM bool decodeIntoPending(
    const uint8_t* data,
    uint32_t size,
    uint8_t versionMinor,
    macro::MacroAutomationBankState& pending
) {
    if (versionMinor != CHUNK_VERSION_MINOR_V14 &&
        versionMinor != CHUNK_VERSION_MINOR_V15) {
        return false;
    }
    if (data == nullptr || size < HEADER_SIZE) return false;
    pending.clear();

    binary::Reader reader(data, size);
    uint8_t entryCount = 0;
    uint8_t reserved0 = 0;
    uint16_t pointCount = 0;
    uint32_t reserved1 = 0;
    if (!reader.readU8(entryCount) ||
        !reader.readU8(reserved0) ||
        !reader.readU16(pointCount) ||
        !reader.readU32(reserved1) ||
        reserved0 != 0U || reserved1 != 0U ||
        entryCount > macro::MACRO_AUTOMATION_SLOT_CAPACITY ||
        pointCount > macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY ||
        payloadSize(entryCount, pointCount) != size ||
        size > MAX_PAYLOAD_SIZE) {
        return false;
    }

    pending.entryCount = entryCount;
    const bool v14 = versionMinor == CHUNK_VERSION_MINOR_V14;
    for (uint8_t index = 0; index < entryCount; ++index) {
        auto& entry = pending.entries[index];
        uint8_t reserved = 0;
        if (!reader.readU8(entry.address.track) ||
            !reader.readU8(entry.address.page) ||
            !reader.readU8(entry.address.macro) ||
            !reader.readU8(reserved) || reserved != 0U ||
            !readSlotState(reader, entry.state, v14)) {
            return false;
        }
        entry.active = true;
    }

    pending.pointPool.used = pointCount;
    for (uint16_t index = 0; index < pointCount; ++index) {
        auto& point = pending.pointPool.points[index];
        if (!reader.readU16(point.tick) || !reader.readI16(point.value)) {
            return false;
        }
    }
    if (!reader.ok() || reader.offset() != size || !validBank(pending)) {
        return false;
    }
    return true;
}

FLASHMEM bool decode(
    const uint8_t* data,
    uint32_t size,
    uint8_t versionMinor,
    macro::MacroAutomationBankState& out
) {
    auto pending = core::app::makeExtmemUnique<macro::MacroAutomationBankState>();
    if (!pending || !decodeIntoPending(data, size, versionMinor, *pending)) {
        return false;
    }
    out = *pending;
    return true;
}

}  // namespace core::persistence::macro_automation_legacy_codec
