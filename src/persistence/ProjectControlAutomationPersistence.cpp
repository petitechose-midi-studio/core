#include "persistence/ProjectControlPersistenceCodecInternal.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

namespace core::persistence::project_control_codec::internal {

namespace {

using AutomationCurveIds = std::array<
    modulation::ProjectCurveId,
    modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
>;

FLASHMEM const modulation::ProjectAutomationCurveEntry* automationAtOrder(
    const modulation::ProjectAutomationCurveDirectory& directory,
    uint16_t order
) {
    for (uint16_t index = 0; index < directory.entryCount; ++index) {
        const uint16_t address = modulation::modulationDestinationStableAddress(
            directory.entries[index].destination
        );
        uint16_t rank = 0;
        for (uint16_t other = 0; other < directory.entryCount; ++other) {
            if (modulation::modulationDestinationStableAddress(
                    directory.entries[other].destination
                ) < address) {
                ++rank;
            }
        }
        if (rank == order) return &directory.entries[index];
    }
    return nullptr;
}

FLASHMEM bool buildAutomationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out,
    AutomationCurveIds& curveIds
) {
    out = {};
    curveIds = {};
    uint32_t points = 0;
    for (uint16_t order = 0; order < source.automation.entryCount; ++order) {
        const auto* entry = automationAtOrder(source.automation, order);
        if (entry == nullptr) return false;
        if (containsCurve(curveIds, out.curveCount, entry->curveId)) continue;
        if (out.curveCount >= curveIds.size()) return false;
        const auto* curve = modulation::findProjectCurve(
            source.curves,
            entry->curveId
        );
        if (curve == nullptr ||
            points + curve->pointCount > modulation::PROJECT_CURVE_POINT_CAPACITY) {
            return false;
        }
        curveIds[out.curveCount++] = entry->curveId;
        points += curve->pointCount;
    }
    out.pointCount = static_cast<uint16_t>(points);
    out.size = PROJECT_CONTROL_CHUNK_HEADER_SIZE +
        static_cast<uint32_t>(source.automation.entryCount) *
            PROJECT_AUTOMATION_ENTRY_SIZE +
        static_cast<uint32_t>(out.curveCount) *
            PROJECT_CONTROL_CURVE_RECORD_SIZE +
        points * PROJECT_CONTROL_CURVE_POINT_SIZE;
    return true;
}

}  // namespace

FLASHMEM bool automationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out
) {
    AutomationCurveIds curveIds{};
    return buildAutomationLayout(source, out, curveIds);
}

FLASHMEM bool writeAutomationPayload(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t capacity,
    uint32_t expectedSize
) {
    PayloadLayout layout{};
    AutomationCurveIds curveIds{};
    if (!buildAutomationLayout(source, layout, curveIds) ||
        layout.size != expectedSize) {
        return false;
    }

    binary::Writer writer(out, capacity);
    if (!writer.writeU16(source.automation.entryCount) ||
        !writer.writeU16(layout.curveCount) ||
        !writer.writeU16(layout.pointCount) ||
        !writer.writeU16(0U) ||
        !writer.writeZeroes(24U)) {
        return false;
    }
    for (uint16_t order = 0; order < source.automation.entryCount; ++order) {
        const auto* entry = automationAtOrder(source.automation, order);
        const int16_t curve = entry == nullptr ? -1 : localCurveIndex(
            curveIds,
            layout.curveCount,
            entry->curveId
        );
        if (entry == nullptr || curve < 0 ||
            !writeDestination(writer, entry->destination) ||
            !writer.writeU16(static_cast<uint16_t>(curve)) ||
            !writer.writeU8(entry->flags) ||
            !writer.writeU8(0U)) {
            return false;
        }
    }
    uint16_t pointOffset = 0;
    for (uint16_t index = 0; index < layout.curveCount; ++index) {
        const auto* curve = modulation::findProjectCurve(
            source.curves,
            curveIds[index]
        );
        if (curve == nullptr || !writeCurveRecord(writer, *curve, pointOffset)) {
            return false;
        }
        pointOffset = static_cast<uint16_t>(pointOffset + curve->pointCount);
    }
    for (uint16_t index = 0; index < layout.curveCount; ++index) {
        const auto* curve = modulation::findProjectCurve(
            source.curves,
            curveIds[index]
        );
        if (curve == nullptr || !writeCurvePoints(writer, source.curves, *curve)) {
            return false;
        }
    }
    return writer.ok() && writer.offset() == expectedSize;
}

FLASHMEM ChunkStatus decodeAutomationCurrent(
    const ChunkPayloadView& view,
    modulation::ProjectControlDomainState& target
) {
    if (view.flags != 0U || view.data == nullptr ||
        view.size < PROJECT_CONTROL_CHUNK_HEADER_SIZE) {
        return ChunkStatus::INVALID_PAYLOAD;
    }
    binary::Reader reader(view.data, view.size);
    uint16_t entryCount = 0;
    uint16_t curveCount = 0;
    uint16_t pointCount = 0;
    uint16_t flags = 0;
    if (!reader.readU16(entryCount) ||
        !reader.readU16(curveCount) ||
        !reader.readU16(pointCount) ||
        !reader.readU16(flags) || flags != 0U ||
        !readZeroes(reader, 24U)) {
        return ChunkStatus::INVALID_PAYLOAD;
    }
    if (entryCount > modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY ||
        curveCount > modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY ||
        curveCount > modulation::PROJECT_CURVE_LIVE_CAPACITY ||
        pointCount > modulation::PROJECT_CURVE_POINT_CAPACITY) {
        return ChunkStatus::CAPACITY_EXCEEDED;
    }
    const uint32_t required = PROJECT_CONTROL_CHUNK_HEADER_SIZE +
        static_cast<uint32_t>(entryCount) * PROJECT_AUTOMATION_ENTRY_SIZE +
        static_cast<uint32_t>(curveCount) * PROJECT_CONTROL_CURVE_RECORD_SIZE +
        static_cast<uint32_t>(pointCount) * PROJECT_CONTROL_CURVE_POINT_SIZE;
    if (required != view.size || required > PROJECT_AUTOMATION_MAX_PAYLOAD_SIZE) {
        return ChunkStatus::INVALID_PAYLOAD;
    }

    target.automation.entryCount = entryCount;
    const uint32_t firstCurveId = target.curves.nextCurveId;
    uint16_t previousAddress = 0;
    for (uint16_t index = 0; index < entryCount; ++index) {
        auto& entry = target.automation.entries[index];
        uint16_t localCurve = 0;
        uint8_t reserved = 0;
        entry = {};
        if (!readDestination(reader, entry.destination) ||
            !reader.readU16(localCurve) ||
            !reader.readU8(entry.flags) ||
            !reader.readU8(reserved) || reserved != 0U ||
            localCurve >= curveCount ||
            (entry.flags &
             ~modulation::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U) {
            target.clear();
            return ChunkStatus::INVALID_PAYLOAD;
        }
        const uint16_t address = modulation::modulationDestinationStableAddress(
            entry.destination
        );
        if (index > 0U && address <= previousAddress) {
            target.clear();
            return ChunkStatus::INVALID_PAYLOAD;
        }
        previousAddress = address;
        entry.curveId = {firstCurveId + localCurve};
    }

    uint16_t localPointOffset = 0;
    for (uint16_t index = 0; index < curveCount; ++index) {
        auto& curve = target.curves.records[index];
        if (!readCurveRecord(
                reader,
                curve,
                {firstCurveId + index},
                0U,
                localPointOffset,
                modulation::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
                false,
                true
            )) {
            target.clear();
            return ChunkStatus::INVALID_PAYLOAD;
        }
        for (uint16_t entry = 0; entry < entryCount; ++entry) {
            if (target.automation.entries[entry].curveId == curve.id) {
                ++curve.referenceCount;
            }
        }
        if (curve.pointCount > pointCount - localPointOffset) {
            target.clear();
            return ChunkStatus::INVALID_PAYLOAD;
        }
        localPointOffset = static_cast<uint16_t>(
            localPointOffset + curve.pointCount
        );
    }
    if (localPointOffset != pointCount) {
        target.clear();
        return ChunkStatus::INVALID_PAYLOAD;
    }
    target.curves.recordCount = curveCount;
    target.curves.pointCount = pointCount;
    target.curves.nextCurveId = firstCurveId + curveCount;
    for (uint16_t index = 0; index < pointCount; ++index) {
        auto& point = target.curves.points[index];
        if (!reader.readU16(point.tick) || !reader.readI16(point.value)) {
            target.clear();
            return ChunkStatus::INVALID_PAYLOAD;
        }
    }
    if (!reader.ok() || reader.offset() != view.size ||
        !modulation::validProjectModulationDomain(
            target.modulation,
            target.curves,
            &target.automation
        )) {
        target.clear();
        return ChunkStatus::INVALID_PAYLOAD;
    }
    return ChunkStatus::CURRENT;
}

}  // namespace core::persistence::project_control_codec::internal
