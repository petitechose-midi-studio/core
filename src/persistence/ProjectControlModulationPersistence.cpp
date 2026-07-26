#include "persistence/ProjectControlPersistenceCodecInternal.hpp"

#include <algorithm>
#include <array>

#include <config/PlatformCompat.hpp>

namespace core::persistence::project_control_codec::internal {

namespace {

using ModulationCurveIds = std::array<
    modulation::ProjectCurveId,
    modulation::PROJECT_MODULATOR_CAPACITY
>;

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

FLASHMEM bool buildModulationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out,
    ModulationCurveIds& curveIds
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

}  // namespace

FLASHMEM bool modulationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out
) {
    ModulationCurveIds curveIds{};
    return buildModulationLayout(source, out, curveIds);
}

FLASHMEM bool writeModulationPayload(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t capacity,
    uint32_t expectedSize
) {
    PayloadLayout layout{};
    ModulationCurveIds curveIds{};
    if (!buildModulationLayout(source, layout, curveIds) ||
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
            if (!writer.writeU16(adsr.delay) ||
                !writer.writeU16(adsr.attack) ||
                !writer.writeU16(adsr.hold) ||
                !writer.writeU16(adsr.decay) ||
                !writer.writeU16(adsr.release) ||
                !writer.writeU16(adsr.sustainQ15) ||
                !writer.writeU16(adsr.smooth) ||
                !writer.writeU16(adsr.traits)) {
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
            !writer.writeU8(binding->trigger.noteMin) ||
            !writer.writeU8(binding->trigger.noteMax) ||
            !writer.writeU8(binding->velocityMin) ||
            !writer.writeU8(binding->velocityMax) ||
            !writer.writeU8(binding->flags) ||
            !writer.writeU8(binding->reserved)) {
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
        uint8_t kind = 0;
        uint16_t payloadIndex = 0;
        uint16_t reserved0 = 0;
        uint16_t reserved1 = 0;
        source = {};
        if (!reader.readU32(source.id.value) ||
            !reader.readBytes(source.name.data(), source.name.size()) ||
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
            auto& adsr = source.parameters.adsr;
            if (!reader.readU16(adsr.delay) ||
                !reader.readU16(adsr.attack) ||
                !reader.readU16(adsr.hold) ||
                !reader.readU16(adsr.decay) ||
                !reader.readU16(adsr.release) ||
                !reader.readU16(adsr.sustainQ15) ||
                !reader.readU16(adsr.smooth) ||
                !reader.readU16(adsr.traits)) {
                return fail(ChunkStatus::INVALID_PAYLOAD);
            }
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
        if (application > static_cast<uint8_t>(
                modulation::ModulationApplication::FROM_BASE
            )) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
        binding.application =
            static_cast<modulation::ModulationApplication>(application);
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
            !reader.readU8(binding.trigger.noteMin) ||
            !reader.readU8(binding.trigger.noteMax) ||
            !reader.readU8(binding.velocityMin) ||
            !reader.readU8(binding.velocityMax) ||
            !reader.readU8(binding.flags) ||
            !reader.readU8(binding.reserved)) {
            return fail(ChunkStatus::INVALID_PAYLOAD);
        }
        if (binding.id.value == 0U || binding.id.value <= previousId) {
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
                true,
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
    return ChunkStatus::CURRENT;
}

}  // namespace core::persistence::project_control_codec::internal
