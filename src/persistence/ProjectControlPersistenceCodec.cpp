#include "persistence/ProjectControlPersistenceCodec.hpp"

#include <algorithm>
#include <array>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/LegacyMacroAutomationPersistenceCodec.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/ProjectControlLegacyMigration.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::persistence::project_control_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace legacy_codec =
    core::persistence::macro_automation_legacy_codec;
namespace migration = core::persistence::project_control_migration;
namespace macro = core::state::macro;
namespace modulation = core::state::modulation;

struct PayloadLayout {
    uint16_t curveCount = 0;
    uint16_t pointCount = 0;
    uint32_t size = 0;
};

template <size_t Capacity>
FLASHMEM bool containsCurve(
    const std::array<modulation::ProjectCurveId, Capacity>& ids,
    uint16_t count,
    modulation::ProjectCurveId id
) {
    for (uint16_t index = 0; index < count; ++index) {
        if (ids[index] == id) return true;
    }
    return false;
}

template <size_t Capacity>
FLASHMEM int16_t localCurveIndex(
    const std::array<modulation::ProjectCurveId, Capacity>& ids,
    uint16_t count,
    modulation::ProjectCurveId id
) {
    for (uint16_t index = 0; index < count; ++index) {
        if (ids[index] == id) return static_cast<int16_t>(index);
    }
    return -1;
}

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

FLASHMEM const modulation::ModulatorSourceState* sourceAtOrder(
    const modulation::ProjectModulationState& state,
    uint16_t order
) {
    for (uint16_t index = 0; index < state.sourceCount; ++index) {
        uint16_t rank = 0;
        for (uint16_t other = 0; other < state.sourceCount; ++other) {
            if (state.sources[other].id.value < state.sources[index].id.value) {
                ++rank;
            }
        }
        if (rank == order) return &state.sources[index];
    }
    return nullptr;
}

FLASHMEM const modulation::ModulationBindingState* outputBindingAtOrder(
    const modulation::ProjectModulationState& state,
    uint16_t order
) {
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        uint16_t rank = 0;
        for (uint16_t other = 0; other < state.outputBindingCount; ++other) {
            if (state.outputBindings[other].id.value <
                state.outputBindings[index].id.value) {
                ++rank;
            }
        }
        if (rank == order) return &state.outputBindings[index];
    }
    return nullptr;
}

FLASHMEM const modulation::ModulationTriggerBindingState* triggerAtOrder(
    const modulation::ProjectModulationState& state,
    uint16_t order
) {
    for (uint16_t index = 0; index < state.triggerBindingCount; ++index) {
        uint16_t rank = 0;
        for (uint16_t other = 0; other < state.triggerBindingCount; ++other) {
            if (state.triggerBindings[other].id.value <
                state.triggerBindings[index].id.value) {
                ++rank;
            }
        }
        if (rank == order) return &state.triggerBindings[index];
    }
    return nullptr;
}

FLASHMEM bool automationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out,
    std::array<
        modulation::ProjectCurveId,
        modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
    >& curveIds
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

FLASHMEM bool modulationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out,
    std::array<
        modulation::ProjectCurveId,
        modulation::PROJECT_MODULATOR_CAPACITY
    >& curveIds
) {
    out = {};
    curveIds = {};
    uint32_t points = 0;
    for (uint16_t order = 0; order < source.modulation.sourceCount; ++order) {
        const auto* state = sourceAtOrder(source.modulation, order);
        if (state == nullptr) return false;
        if (state->kind != modulation::ModulatorKind::RECORDED_SHAPE ||
            containsCurve(
                curveIds,
                out.curveCount,
                state->parameters.recordedCurveId
            )) {
            continue;
        }
        if (out.curveCount >= curveIds.size()) return false;
        const auto* curve = modulation::findProjectCurve(
            source.curves,
            state->parameters.recordedCurveId
        );
        if (curve == nullptr ||
            points + curve->pointCount > modulation::PROJECT_CURVE_POINT_CAPACITY) {
            return false;
        }
        curveIds[out.curveCount++] = state->parameters.recordedCurveId;
        points += curve->pointCount;
    }
    out.pointCount = static_cast<uint16_t>(points);
    out.size = PROJECT_CONTROL_CHUNK_HEADER_SIZE +
        static_cast<uint32_t>(source.modulation.sourceCount) *
            PROJECT_MODULATOR_SOURCE_DIRECTORY_SIZE +
        static_cast<uint32_t>(source.modulation.sourceCount) *
            PROJECT_MODULATOR_SOURCE_PAYLOAD_SIZE +
        static_cast<uint32_t>(source.modulation.outputBindingCount) *
            PROJECT_MODULATION_BINDING_SIZE +
        static_cast<uint32_t>(source.modulation.triggerBindingCount) *
            PROJECT_MODULATION_TRIGGER_SIZE +
        static_cast<uint32_t>(source.modulation.destinationScaleCount) *
            PROJECT_MODULATION_DESTINATION_SCALE_SIZE +
        static_cast<uint32_t>(out.curveCount) *
            PROJECT_CONTROL_CURVE_RECORD_SIZE +
        points * PROJECT_CONTROL_CURVE_POINT_SIZE;
    return true;
}

FLASHMEM bool writeDestination(
    binary::Writer& writer,
    const modulation::ModulationDestination& destination
) {
    return writer.writeU8(static_cast<uint8_t>(destination.kind)) &&
           writer.writeU8(destination.track) &&
           writer.writeU8(destination.page) &&
           writer.writeU8(destination.macro);
}

FLASHMEM bool readDestination(
    binary::Reader& reader,
    modulation::ModulationDestination& destination
) {
    uint8_t kind = 0;
    if (!reader.readU8(kind) ||
        !reader.readU8(destination.track) ||
        !reader.readU8(destination.page) ||
        !reader.readU8(destination.macro)) {
        return false;
    }
    destination.kind = static_cast<modulation::ModulationDestinationKind>(kind);
    return modulation::modulationDestinationValid(destination);
}

FLASHMEM bool writeCurveRecord(
    binary::Writer& writer,
    const modulation::ProjectCurveRecord& record,
    uint16_t localPointOffset
) {
    return writer.writeU16(localPointOffset) &&
           writer.writeU16(record.pointCount) &&
           writer.writeU16(record.sourceDurationTicks) &&
           writer.writeU16(record.durationTicks) &&
           writer.writeU16(record.windowOffsetTicks) &&
           writer.writeU8(static_cast<uint8_t>(record.interpolation)) &&
           writer.writeU8(static_cast<uint8_t>(record.valueDomain)) &&
           writer.writeU8(static_cast<uint8_t>(record.origin)) &&
           writer.writeU8(record.flags) &&
           writer.writeU16(0U) &&
           writer.writeU32(0U);
}

FLASHMEM bool writeCurvePoints(
    binary::Writer& writer,
    const modulation::ProjectCurveArena& arena,
    const modulation::ProjectCurveRecord& record
) {
    for (uint16_t index = 0; index < record.pointCount; ++index) {
        const auto& point = arena.points[
            static_cast<uint16_t>(record.pointOffset + index)
        ];
        if (!writer.writeU16(point.tick) || !writer.writeI16(point.value)) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool writeAutomationPayload(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t capacity,
    uint32_t expectedSize
) {
    PayloadLayout layout{};
    std::array<
        modulation::ProjectCurveId,
        modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
    > curveIds{};
    if (!automationLayout(source, layout, curveIds) ||
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

FLASHMEM bool writeModulationPayload(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t capacity,
    uint32_t expectedSize
) {
    PayloadLayout layout{};
    std::array<
        modulation::ProjectCurveId,
        modulation::PROJECT_MODULATOR_CAPACITY
    > curveIds{};
    if (!modulationLayout(source, layout, curveIds) ||
        layout.size != expectedSize) {
        return false;
    }

    const auto& graph = source.modulation;
    binary::Writer writer(out, capacity);
    if (!writer.writeU16(graph.sourceCount) ||
        !writer.writeU16(graph.outputBindingCount) ||
        !writer.writeU16(graph.triggerBindingCount) ||
        !writer.writeU16(layout.curveCount) ||
        !writer.writeU16(layout.pointCount) ||
        !writer.writeU16(graph.destinationScaleCount) ||
        !writer.writeU32(graph.nextSourceId) ||
        !writer.writeU32(graph.nextBindingId) ||
        !writer.writeZeroes(12U)) {
        return false;
    }

    for (uint16_t order = 0; order < graph.sourceCount; ++order) {
        const auto* state = sourceAtOrder(graph, order);
        if (state == nullptr ||
            !writer.writeU32(state->id.value) ||
            !writer.writeBytes(state->name.data(), state->name.size()) ||
            !writer.writeU16(state->reach.trackMask) ||
            !writer.writeU8(static_cast<uint8_t>(state->reach.kind)) ||
            !writer.writeU8(state->reach.track) ||
            !writer.writeU8(state->reach.page) ||
            !writer.writeU8(state->reach.macro) ||
            !writer.writeU8(static_cast<uint8_t>(state->kind)) ||
            !writer.writeU8(state->flags) ||
            !writer.writeU8(state->accent) ||
            !writer.writeU8(state->schemaVersion) ||
            !writer.writeU16(order) ||
            !writer.writeU16(0U) ||
            !writer.writeU16(0U)) {
            return false;
        }
    }
    for (uint16_t order = 0; order < graph.sourceCount; ++order) {
        const auto* state = sourceAtOrder(graph, order);
        if (state == nullptr) return false;
        if (state->kind == modulation::ModulatorKind::RECORDED_SHAPE) {
            const int16_t curve = localCurveIndex(
                curveIds,
                layout.curveCount,
                state->parameters.recordedCurveId
            );
            if (curve < 0 ||
                !writer.writeU16(static_cast<uint16_t>(curve)) ||
                !writer.writeZeroes(14U)) {
                return false;
            }
        } else if (state->kind == modulation::ModulatorKind::LFO) {
            const auto& lfo = state->parameters.lfo;
            if (!writer.writeU32(lfo.periodTicks) ||
                !writer.writeU32(lfo.freePeriodMs) ||
                !writer.writeI16(lfo.phaseQ15) ||
                !writer.writeU8(static_cast<uint8_t>(lfo.shape)) ||
                !writer.writeU8(static_cast<uint8_t>(lfo.retrigger)) ||
                !writer.writeU8(static_cast<uint8_t>(lfo.timing)) ||
                !writer.writeBytes(lfo.reserved.data(), lfo.reserved.size())) {
                return false;
            }
        } else if (state->kind == modulation::ModulatorKind::ADSR) {
            const auto& adsr = state->parameters.adsr;
            if (!writer.writeU16(adsr.attack) ||
                !writer.writeU16(adsr.decay) ||
                !writer.writeU16(adsr.release) ||
                !writer.writeU16(adsr.sustainQ15) ||
                !writer.writeU8(static_cast<uint8_t>(adsr.timing)) ||
                !writer.writeU8(static_cast<uint8_t>(adsr.retrigger)) ||
                !writer.writeU8(static_cast<uint8_t>(adsr.curve)) ||
                !writer.writeU8(adsr.reserved) ||
                !writer.writeZeroes(4U)) {
                return false;
            }
        } else {
            return false;
        }
    }
    for (uint16_t order = 0; order < graph.outputBindingCount; ++order) {
        const auto* binding = outputBindingAtOrder(graph, order);
        if (binding == nullptr ||
            !writer.writeU32(binding->id.value) ||
            !writer.writeU32(binding->sourceId.value) ||
            !writeDestination(writer, binding->destination) ||
            !writer.writeI16(binding->amountQ15) ||
            !writer.writeU8(static_cast<uint8_t>(binding->application)) ||
            !writer.writeU8(static_cast<uint8_t>(binding->transfer)) ||
            !writer.writeU16(binding->slewMs) ||
            !writer.writeU8(binding->flags) ||
            !writer.writeU8(binding->reserved)) {
            return false;
        }
    }
    for (uint16_t order = 0; order < graph.triggerBindingCount; ++order) {
        const auto* binding = triggerAtOrder(graph, order);
        if (binding == nullptr ||
            !writer.writeU32(binding->id.value) ||
            !writer.writeU32(binding->sourceId.value) ||
            !writer.writeU8(static_cast<uint8_t>(binding->trigger.kind)) ||
            !writer.writeU8(binding->trigger.track) ||
            !writer.writeU8(binding->trigger.channel) ||
            !writer.writeU8(binding->trigger.data) ||
            !writer.writeU8(binding->flags) ||
            !writer.writeBytes(
                binding->reserved.data(),
                binding->reserved.size()
            )) {
            return false;
        }
    }
    for (uint16_t index = 0; index < graph.destinationScaleCount; ++index) {
        const auto& entry = graph.destinationScales[index];
        if (!writeDestination(writer, entry.destination) ||
            !writer.writeU16(entry.scaleQ15)) {
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

FLASHMEM bool readZeroes(binary::Reader& reader, uint32_t count) {
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t value = 0;
        if (!reader.readU8(value) || value != 0U) return false;
    }
    return true;
}

FLASHMEM bool readCurveRecord(
    binary::Reader& reader,
    modulation::ProjectCurveRecord& record,
    modulation::ProjectCurveId id,
    uint16_t pointBase,
    uint16_t expectedLocalOffset,
    modulation::ProjectCurveValueDomain requiredDomain,
    bool acceptEitherDomain,
    bool requireNativeOrigin
) {
    uint16_t localOffset = 0;
    uint8_t interpolation = 0;
    uint8_t valueDomain = 0;
    uint8_t origin = 0;
    uint8_t flags = 0;
    uint16_t reserved0 = 0;
    uint32_t reserved1 = 0;
    record = {};
    if (!reader.readU16(localOffset) ||
        !reader.readU16(record.pointCount) ||
        !reader.readU16(record.sourceDurationTicks) ||
        !reader.readU16(record.durationTicks) ||
        !reader.readU16(record.windowOffsetTicks) ||
        !reader.readU8(interpolation) ||
        !reader.readU8(valueDomain) ||
        !reader.readU8(origin) ||
        !reader.readU8(flags) ||
        !reader.readU16(reserved0) ||
        !reader.readU32(reserved1) ||
        localOffset != expectedLocalOffset || record.pointCount == 0U ||
        interpolation !=
            static_cast<uint8_t>(modulation::ProjectCurveInterpolation::LINEAR) ||
        valueDomain > static_cast<uint8_t>(
            modulation::ProjectCurveValueDomain::BIPOLAR
        ) ||
        (!acceptEitherDomain &&
         valueDomain != static_cast<uint8_t>(requiredDomain)) ||
        origin > static_cast<uint8_t>(modulation::ProjectCurveOrigin::CONVERTED_MIN) ||
        (requireNativeOrigin &&
         origin != static_cast<uint8_t>(modulation::ProjectCurveOrigin::NATIVE)) ||
        flags != 0U || reserved0 != 0U || reserved1 != 0U) {
        return false;
    }
    record.id = id;
    record.pointOffset = static_cast<uint16_t>(pointBase + localOffset);
    record.interpolation = modulation::ProjectCurveInterpolation::LINEAR;
    record.valueDomain = static_cast<modulation::ProjectCurveValueDomain>(
        valueDomain
    );
    record.origin = static_cast<modulation::ProjectCurveOrigin>(origin);
    return true;
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

FLASHMEM void rollbackGraph(
    modulation::ProjectControlDomainState& target,
    uint16_t recordBase,
    uint16_t pointBase,
    uint32_t nextCurveId
) {
    target.modulation = {};
    std::fill(
        target.curves.records.begin() + recordBase,
        target.curves.records.end(),
        modulation::ProjectCurveRecord{}
    );
    std::fill(
        target.curves.points.begin() + pointBase,
        target.curves.points.end(),
        modulation::ProjectPackedCurvePoint{}
    );
    target.curves.recordCount = recordBase;
    target.curves.pointCount = pointBase;
    target.curves.nextCurveId = nextCurveId;
}

FLASHMEM ChunkStatus decodeModulationCurrent(
    const ChunkPayloadView& view,
    modulation::ProjectControlDomainState& target
) {
    if (view.flags != 0U || view.data == nullptr ||
        view.size < PROJECT_CONTROL_CHUNK_HEADER_SIZE) {
        return ChunkStatus::INVALID_PAYLOAD;
    }
    binary::Reader reader(view.data, view.size);
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    uint16_t triggerCount = 0;
    uint16_t curveCount = 0;
    uint16_t pointCount = 0;
    uint16_t destinationScaleCount = 0;
    uint32_t nextSourceId = 0;
    uint32_t nextBindingId = 0;
    if (!reader.readU16(sourceCount) ||
        !reader.readU16(bindingCount) ||
        !reader.readU16(triggerCount) ||
        !reader.readU16(curveCount) ||
        !reader.readU16(pointCount) ||
        !reader.readU16(destinationScaleCount) ||
        !reader.readU32(nextSourceId) ||
        !reader.readU32(nextBindingId) ||
        !readZeroes(reader, 12U)) {
        return ChunkStatus::INVALID_PAYLOAD;
    }
    if (view.versionMinor <
            PROJECT_MODULATION_GRAPH_GLOBAL_DEPTH_VERSION_MINOR &&
        destinationScaleCount != 0U) {
        return ChunkStatus::INVALID_PAYLOAD;
    }
    if (sourceCount > modulation::PROJECT_MODULATOR_CAPACITY ||
        bindingCount > modulation::PROJECT_MODULATION_BINDING_CAPACITY ||
        triggerCount > modulation::PROJECT_MODULATION_TRIGGER_CAPACITY ||
        destinationScaleCount >
            modulation::PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY ||
        curveCount > modulation::PROJECT_MODULATOR_CAPACITY ||
        pointCount > modulation::PROJECT_CURVE_POINT_CAPACITY ||
        static_cast<uint32_t>(target.curves.recordCount) + curveCount >
            modulation::PROJECT_CURVE_LIVE_CAPACITY ||
        static_cast<uint32_t>(target.curves.pointCount) + pointCount >
            modulation::PROJECT_CURVE_POINT_CAPACITY) {
        return ChunkStatus::CAPACITY_EXCEEDED;
    }
    const uint32_t required = PROJECT_CONTROL_CHUNK_HEADER_SIZE +
        static_cast<uint32_t>(sourceCount) *
            PROJECT_MODULATOR_SOURCE_DIRECTORY_SIZE +
        static_cast<uint32_t>(sourceCount) *
            PROJECT_MODULATOR_SOURCE_PAYLOAD_SIZE +
        static_cast<uint32_t>(bindingCount) * PROJECT_MODULATION_BINDING_SIZE +
        static_cast<uint32_t>(triggerCount) * PROJECT_MODULATION_TRIGGER_SIZE +
        static_cast<uint32_t>(destinationScaleCount) *
            PROJECT_MODULATION_DESTINATION_SCALE_SIZE +
        static_cast<uint32_t>(curveCount) * PROJECT_CONTROL_CURVE_RECORD_SIZE +
        static_cast<uint32_t>(pointCount) * PROJECT_CONTROL_CURVE_POINT_SIZE;
    if (required != view.size ||
        required > PROJECT_MODULATION_GRAPH_MAX_PAYLOAD_SIZE) {
        return ChunkStatus::INVALID_PAYLOAD;
    }

    const uint16_t recordBase = target.curves.recordCount;
    const uint16_t pointBase = target.curves.pointCount;
    const uint32_t firstCurveId = target.curves.nextCurveId;
    auto fail = [&](ChunkStatus status) {
        rollbackGraph(target, recordBase, pointBase, firstCurveId);
        return status;
    };

    auto& graph = target.modulation;
    graph = {};
    graph.nextSourceId = nextSourceId;
    graph.nextBindingId = nextBindingId;
    graph.sourceCount = sourceCount;
    graph.outputBindingCount = bindingCount;
    graph.triggerBindingCount = triggerCount;
    graph.destinationScaleCount = destinationScaleCount;

    uint32_t previousId = 0;
    for (uint16_t index = 0; index < sourceCount; ++index) {
        auto& source = graph.sources[index];
        uint8_t reachKind = 0;
        uint8_t kind = 0;
        uint16_t payloadIndex = 0;
        uint16_t reserved0 = 0;
        uint16_t reserved1 = 0;
        source = {};
        if (!reader.readU32(source.id.value) ||
            !reader.readBytes(source.name.data(), source.name.size()) ||
            !reader.readU16(source.reach.trackMask) ||
            !reader.readU8(reachKind) ||
            !reader.readU8(source.reach.track) ||
            !reader.readU8(source.reach.page) ||
            !reader.readU8(source.reach.macro) ||
            !reader.readU8(kind) ||
            !reader.readU8(source.flags) ||
            !reader.readU8(source.accent) ||
            !reader.readU8(source.schemaVersion) ||
            !reader.readU16(payloadIndex) ||
            !reader.readU16(reserved0) ||
            !reader.readU16(reserved1) ||
            source.id.value == 0U || source.id.value <= previousId ||
            payloadIndex != index || reserved0 != 0U || reserved1 != 0U) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
        previousId = source.id.value;
        source.reach.kind = static_cast<modulation::ModulatorReachKind>(reachKind);
        source.kind = static_cast<modulation::ModulatorKind>(kind);
    }

    for (uint16_t index = 0; index < sourceCount; ++index) {
        auto& source = graph.sources[index];
        if (source.kind == modulation::ModulatorKind::RECORDED_SHAPE) {
            uint16_t localCurve = 0;
            if (!reader.readU16(localCurve) || localCurve >= curveCount ||
                !readZeroes(reader, 14U)) {
                return fail(ChunkStatus::INVALID_PAYLOAD);
            }
            source.parameters.recordedCurveId = {firstCurveId + localCurve};
        } else if (source.kind == modulation::ModulatorKind::LFO) {
            uint8_t shape = 0;
            uint8_t retrigger = 0;
            uint8_t timing = 0;
            auto& lfo = source.parameters.lfo;
            if (!reader.readU32(lfo.periodTicks) ||
                !reader.readU32(lfo.freePeriodMs) ||
                !reader.readI16(lfo.phaseQ15) ||
                !reader.readU8(shape) ||
                !reader.readU8(retrigger) ||
                !reader.readU8(timing) ||
                !reader.readBytes(lfo.reserved.data(), lfo.reserved.size())) {
                return fail(ChunkStatus::INVALID_PAYLOAD);
            }
            lfo.shape = static_cast<modulation::ModulatorLfoShape>(shape);
            lfo.retrigger =
                static_cast<modulation::ModulatorRetriggerPolicy>(retrigger);
            lfo.timing = static_cast<modulation::ModulatorTimingMode>(timing);
        } else if (source.kind == modulation::ModulatorKind::ADSR) {
            if (view.versionMinor <
                PROJECT_MODULATION_GRAPH_ADSR_VERSION_MINOR) {
                return fail(ChunkStatus::INVALID_PAYLOAD);
            }
            uint8_t timing = 0;
            uint8_t retrigger = 0;
            uint8_t curve = 0;
            auto& adsr = source.parameters.adsr;
            if (!reader.readU16(adsr.attack) ||
                !reader.readU16(adsr.decay) ||
                !reader.readU16(adsr.release) ||
                !reader.readU16(adsr.sustainQ15) ||
                !reader.readU8(timing) ||
                !reader.readU8(retrigger) ||
                !reader.readU8(curve) ||
                !reader.readU8(adsr.reserved) ||
                !readZeroes(reader, 4U)) {
                return fail(ChunkStatus::INVALID_PAYLOAD);
            }
            adsr.timing = static_cast<modulation::ModulatorTimingMode>(timing);
            adsr.retrigger =
                static_cast<modulation::ModulatorAdsrRetriggerMode>(retrigger);
            adsr.curve = static_cast<modulation::ModulatorAdsrCurve>(curve);
        } else {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
    }

    previousId = 0;
    for (uint16_t index = 0; index < bindingCount; ++index) {
        auto& binding = graph.outputBindings[index];
        uint8_t application = 0;
        uint8_t transfer = 0;
        binding = {};
        if (!reader.readU32(binding.id.value) ||
            !reader.readU32(binding.sourceId.value) ||
            !readDestination(reader, binding.destination) ||
            !reader.readI16(binding.amountQ15) ||
            !reader.readU8(application) ||
            !reader.readU8(transfer) ||
            !reader.readU16(binding.slewMs) ||
            !reader.readU8(binding.flags) ||
            !reader.readU8(binding.reserved) ||
            binding.id.value == 0U || binding.id.value <= previousId) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
        previousId = binding.id.value;
        if (view.versionMinor ==
            PROJECT_MODULATION_GRAPH_LEGACY_VERSION_MINOR) {
            if (application > 1U) {
                return fail(ChunkStatus::INVALID_PAYLOAD);
            }
            binding.application = application == 0U
                ? modulation::ModulationApplication::AROUND_BASE
                : modulation::ModulationApplication::FROM_BASE;
        } else {
            if (application > static_cast<uint8_t>(
                    modulation::ModulationApplication::FROM_BASE
                )) {
                return fail(ChunkStatus::INVALID_PAYLOAD);
            }
            binding.application =
                static_cast<modulation::ModulationApplication>(application);
        }
        binding.transfer = static_cast<modulation::ModulationTransfer>(transfer);
    }

    previousId = 0;
    for (uint16_t index = 0; index < triggerCount; ++index) {
        auto& binding = graph.triggerBindings[index];
        uint8_t kind = 0;
        binding = {};
        if (!reader.readU32(binding.id.value) ||
            !reader.readU32(binding.sourceId.value) ||
            !reader.readU8(kind) ||
            !reader.readU8(binding.trigger.track) ||
            !reader.readU8(binding.trigger.channel) ||
            !reader.readU8(binding.trigger.data) ||
            !reader.readU8(binding.flags) ||
            !reader.readBytes(
                binding.reserved.data(),
                binding.reserved.size()
            ) || binding.id.value == 0U || binding.id.value <= previousId) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
        previousId = binding.id.value;
        binding.trigger.kind =
            static_cast<modulation::ModulationTriggerKind>(kind);
    }

    for (uint16_t index = 0; index < destinationScaleCount; ++index) {
        auto& entry = graph.destinationScales[index];
        if (!readDestination(reader, entry.destination) ||
            !reader.readU16(entry.scaleQ15)) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
    }

    uint16_t localPointOffset = 0;
    for (uint16_t index = 0; index < curveCount; ++index) {
        auto& curve = target.curves.records[
            static_cast<uint16_t>(recordBase + index)
        ];
        if (!readCurveRecord(
                reader,
                curve,
                {firstCurveId + index},
                pointBase,
                localPointOffset,
                modulation::ProjectCurveValueDomain::BIPOLAR,
                view.versionMinor >=
                    PROJECT_MODULATION_GRAPH_NATURAL_APPLICATION_VERSION_MINOR,
                false
            )) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
        for (uint16_t source = 0; source < sourceCount; ++source) {
            if (graph.sources[source].kind ==
                    modulation::ModulatorKind::RECORDED_SHAPE &&
                graph.sources[source].parameters.recordedCurveId == curve.id) {
                ++curve.referenceCount;
            }
        }
        if (curve.pointCount > pointCount - localPointOffset) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
        localPointOffset = static_cast<uint16_t>(
            localPointOffset + curve.pointCount
        );
    }
    if (localPointOffset != pointCount) {
        return fail(ChunkStatus::INVALID_PAYLOAD);
    }
    target.curves.recordCount = static_cast<uint16_t>(recordBase + curveCount);
    target.curves.pointCount = static_cast<uint16_t>(pointBase + pointCount);
    target.curves.nextCurveId = firstCurveId + curveCount;
    for (uint16_t index = 0; index < pointCount; ++index) {
        auto& point = target.curves.points[
            static_cast<uint16_t>(pointBase + index)
        ];
        if (!reader.readU16(point.tick) || !reader.readI16(point.value)) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
    }
    if (!reader.ok() || reader.offset() != view.size ||
        !modulation::validProjectModulationDomain(
            target.modulation,
            target.curves,
            &target.automation
        )) {
        return fail(ChunkStatus::INVALID_PAYLOAD);
    }
    return view.versionMinor == PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR
        ? ChunkStatus::CURRENT
        : ChunkStatus::MIGRATED_LEGACY;
}

FLASHMEM bool isLegacyAutomationVersion(const ChunkPayloadView& view) {
    return view.present && view.versionMajor == legacy_codec::CHUNK_VERSION_MAJOR &&
        (view.versionMinor == legacy_codec::CHUNK_VERSION_MINOR_V14 ||
         view.versionMinor == legacy_codec::CHUNK_VERSION_MINOR_V15);
}

FLASHMEM Status migrateLegacy(
    const ChunkPayloadView& view,
    modulation::ProjectControlDomainState& target,
    bool& migrated
) {
    migrated = false;
    if (view.flags != 0U) return Status::INVALID_DOMAIN;
    auto legacy = core::app::makeExtmemUnique<macro::MacroAutomationBankState>();
    if (!legacy) return Status::SCRATCH_ALLOCATION_FAILED;
    if (!legacy_codec::decodeIntoPending(
            view.data,
            view.size,
            view.versionMinor,
            *legacy
        )) {
        return Status::INVALID_DOMAIN;
    }
    const auto result = migration::liftLegacyMacroAutomationBankIntoPending(
        *legacy,
        target
    );
    if (result.status == migration::Status::SCRATCH_ALLOCATION_FAILED) {
        return Status::SCRATCH_ALLOCATION_FAILED;
    }
    if (!result.migrated()) return Status::INVALID_DOMAIN;
    migrated = true;
    return Status::OK;
}

}  // namespace

FLASHMEM EncodeResult encodeProjectControlPayloads(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t outCapacity
) {
    if (out == nullptr) return {.status = Status::INVALID_ARGUMENT};
    if (!modulation::validProjectModulationDomain(
            source.modulation,
            source.curves,
            &source.automation
        )) {
        return {.status = Status::INVALID_DOMAIN};
    }

    PayloadLayout automation{};
    PayloadLayout graph{};
    std::array<
        modulation::ProjectCurveId,
        modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
    > automationCurveIds{};
    std::array<
        modulation::ProjectCurveId,
        modulation::PROJECT_MODULATOR_CAPACITY
    > modulationCurveIds{};
    if (!automationLayout(source, automation, automationCurveIds) ||
        !modulationLayout(source, graph, modulationCurveIds) ||
        static_cast<uint32_t>(automation.pointCount) + graph.pointCount !=
            source.curves.pointCount ||
        automation.size + graph.size >
            PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE) {
        return {.status = Status::INVALID_DOMAIN};
    }

    const uint32_t required = automation.size + graph.size;
    if (required > outCapacity) {
        return {
            .status = Status::BUFFER_TOO_SMALL,
            .bytesRequired = required,
        };
    }
    if (!writeAutomationPayload(
            source,
            out,
            automation.size,
            automation.size
        ) ||
        !writeModulationPayload(
            source,
            out + automation.size,
            graph.size,
            graph.size
        )) {
        return {
            .status = Status::INVALID_DOMAIN,
            .bytesRequired = required,
        };
    }
    return {
        .status = Status::OK,
        .bytesRequired = required,
        .bytesWritten = required,
        .automationOffset = 0U,
        .automationSize = automation.size,
        .modulationOffset = automation.size,
        .modulationSize = graph.size,
    };
}

FLASHMEM DecodeResult decodeProjectControlPayloads(
    const ChunkPayloadView& automation,
    const ChunkPayloadView& modulationView,
    modulation::ProjectControlDomainState& out
) {
    auto pending =
        core::app::makeExtmemUnique<modulation::ProjectControlDomainState>();
    if (!pending) {
        return {.status = Status::SCRATCH_ALLOCATION_FAILED};
    }

    DecodeResult result{.status = Status::OK};
    if (isLegacyAutomationVersion(automation)) {
        bool migrated = false;
        const Status migrationStatus = migrateLegacy(
            automation,
            *pending,
            migrated
        );
        if (migrationStatus == Status::SCRATCH_ALLOCATION_FAILED) {
            return {.status = migrationStatus};
        }
        if (migrated) {
            result.automationStatus = ChunkStatus::MIGRATED_LEGACY;
            result.modulationStatus = modulationView.present
                ? ChunkStatus::IGNORED_AMBIGUOUS
                : ChunkStatus::MIGRATED_LEGACY;
            result.migratedLegacy = true;
            result.partial = modulationView.present;
            result.overwriteSafe = !modulationView.present;
        } else {
            pending->clear();
            result.automationStatus = ChunkStatus::INVALID_PAYLOAD;
            result.modulationStatus = modulationView.present
                ? ChunkStatus::IGNORED_AMBIGUOUS
                : ChunkStatus::MISSING;
            result.partial = true;
            result.overwriteSafe = false;
        }
        out = *pending;
        return result;
    }

    if (!automation.present) {
        result.automationStatus = ChunkStatus::MISSING;
    } else if (automation.versionMajor != PROJECT_CONTROL_CHUNK_VERSION_MAJOR ||
               automation.versionMinor !=
                   PROJECT_AUTOMATION_CHUNK_VERSION_MINOR) {
        result.automationStatus = ChunkStatus::UNSUPPORTED_VERSION;
    } else {
        result.automationStatus = decodeAutomationCurrent(
            automation,
            *pending
        );
    }

    if (!modulationView.present) {
        result.modulationStatus = ChunkStatus::MISSING;
    } else if (modulationView.versionMajor !=
                   PROJECT_CONTROL_CHUNK_VERSION_MAJOR ||
                (modulationView.versionMinor !=
                     PROJECT_MODULATION_GRAPH_LEGACY_VERSION_MINOR &&
                 modulationView.versionMinor !=
                     PROJECT_MODULATION_GRAPH_NATURAL_APPLICATION_VERSION_MINOR &&
                 modulationView.versionMinor !=
                     PROJECT_MODULATION_GRAPH_GLOBAL_DEPTH_VERSION_MINOR &&
                 modulationView.versionMinor !=
                    PROJECT_MODULATION_GRAPH_ADSR_VERSION_MINOR)) {
        result.modulationStatus = ChunkStatus::UNSUPPORTED_VERSION;
    } else {
        result.modulationStatus = decodeModulationCurrent(
            modulationView,
            *pending
        );
        if (result.modulationStatus == ChunkStatus::MIGRATED_LEGACY) {
            result.migratedLegacy = true;
        }
    }

    const bool bothMissing = !automation.present && !modulationView.present;
    const bool bothCurrent =
        result.automationStatus == ChunkStatus::CURRENT &&
        (result.modulationStatus == ChunkStatus::CURRENT ||
         result.modulationStatus == ChunkStatus::MIGRATED_LEGACY);
    result.partial = !bothMissing && !bothCurrent;
    result.overwriteSafe = !result.partial;
    out = *pending;
    return result;
}

}  // namespace core::persistence::project_control_codec
