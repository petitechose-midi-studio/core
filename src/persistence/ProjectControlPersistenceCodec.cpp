#include "persistence/ProjectControlPersistenceCodec.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "app/ExtmemAllocator.hpp"
#include "diagnostics/MemoryFootprintReporter.hpp"
#include "persistence/ProjectControlPersistenceCodecInternal.hpp"

namespace core::persistence::project_control_codec {

namespace modulation = core::state::modulation;

namespace internal {

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
        origin > static_cast<uint8_t>(
            modulation::ProjectCurveOrigin::CONVERTED_MIN
        ) ||
        (requireNativeOrigin &&
         origin != static_cast<uint8_t>(
             modulation::ProjectCurveOrigin::NATIVE
         )) ||
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

}  // namespace internal

FLASHMEM EncodeResult encodeProjectControlPayloads(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t outCapacity
) {
    OC_PERF_SCOPE(perfEncode, "persistence.project-control.encode");
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.control-encode-begin"
    );
#endif
    if (out == nullptr) return {.status = Status::INVALID_ARGUMENT};
    if (!modulation::validProjectModulationDomain(
            source.modulation,
            source.curves,
            &source.automation
        )) {
        return {.status = Status::INVALID_DOMAIN};
    }

    internal::PayloadLayout automation{};
    internal::PayloadLayout graph{};
    if (!internal::automationLayout(source, automation) ||
        !internal::modulationLayout(source, graph) ||
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
    bool automationWritten = false;
    {
        OC_PERF_SCOPE(
            perfAutomation,
            "persistence.project-control.encode.automation"
        );
        automationWritten = internal::writeAutomationPayload(
            source,
            out,
            automation.size,
            automation.size
        );
        OC_PERF_UNITS(
            perfAutomation,
            automation.size,
            automation.pointCount
        );
    }
    bool modulationWritten = false;
    if (automationWritten) {
        OC_PERF_SCOPE(
            perfModulation,
            "persistence.project-control.encode.modulation"
        );
        modulationWritten = internal::writeModulationPayload(
            source,
            out + automation.size,
            graph.size,
            graph.size
        );
        OC_PERF_UNITS(perfModulation, graph.size, graph.pointCount);
    }
    if (!automationWritten || !modulationWritten) {
        return {
            .status = Status::INVALID_DOMAIN,
            .bytesRequired = required,
        };
    }
    OC_PERF_UNITS(perfEncode, required, source.curves.pointCount);
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.control-encode-end"
    );
#endif
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
    OC_PERF_SCOPE(perfDecode, "persistence.project-control.decode");
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.control-decode-begin"
    );
#endif
    auto pending =
        core::app::makeExtmemUnique<modulation::ProjectControlDomainState>();
    if (!pending) {
        return {.status = Status::SCRATCH_ALLOCATION_FAILED};
    }

    DecodeResult result{.status = Status::OK};
    if (!automation.present) {
        result.automationStatus = ChunkStatus::MISSING;
    } else if (automation.versionMajor != PROJECT_CONTROL_CHUNK_VERSION_MAJOR ||
               automation.versionMinor !=
                   PROJECT_AUTOMATION_CHUNK_VERSION_MINOR) {
        result.automationStatus = ChunkStatus::UNSUPPORTED_VERSION;
    } else {
        OC_PERF_SCOPE(
            perfAutomation,
            "persistence.project-control.decode.automation"
        );
        result.automationStatus =
            internal::decodeAutomationCurrent(automation, *pending);
        OC_PERF_UNITS(
            perfAutomation,
            automation.size,
            pending->automation.entryCount
        );
    }

    if (!modulationView.present) {
        result.modulationStatus = ChunkStatus::MISSING;
    } else if (modulationView.versionMajor !=
                   PROJECT_CONTROL_CHUNK_VERSION_MAJOR ||
               modulationView.versionMinor !=
                   PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR) {
        result.modulationStatus = ChunkStatus::UNSUPPORTED_VERSION;
    } else {
        OC_PERF_SCOPE(
            perfModulation,
            "persistence.project-control.decode.modulation"
        );
        result.modulationStatus =
            internal::decodeModulationCurrent(modulationView, *pending);
        OC_PERF_UNITS(
            perfModulation,
            modulationView.size,
            pending->modulation.sourceCount
        );
    }

    const bool bothMissing = !automation.present && !modulationView.present;
    const bool bothCurrent =
        result.automationStatus == ChunkStatus::CURRENT &&
        result.modulationStatus == ChunkStatus::CURRENT;
    result.partial = !bothMissing && !bothCurrent;
    result.overwriteSafe = !result.partial;
    out = *pending;
    OC_PERF_UNITS(
        perfDecode,
        automation.size + modulationView.size,
        pending->curves.pointCount
    );
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.control-decode-end"
    );
#endif
    return result;
}

}  // namespace core::persistence::project_control_codec
