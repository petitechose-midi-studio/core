#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/ProjectControlPersistenceCodec.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::persistence::project_control_codec::internal {

namespace binary = core::persistence::binary_codec;
namespace modulation = core::state::modulation;

struct PayloadLayout {
    uint16_t curveCount = 0;
    uint16_t pointCount = 0;
    uint32_t size = 0;
};

template <size_t Capacity>
bool containsCurve(
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
int16_t localCurveIndex(
    const std::array<modulation::ProjectCurveId, Capacity>& ids,
    uint16_t count,
    modulation::ProjectCurveId id
) {
    for (uint16_t index = 0; index < count; ++index) {
        if (ids[index] == id) return static_cast<int16_t>(index);
    }
    return -1;
}

bool writeDestination(
    binary::Writer& writer,
    const modulation::ModulationDestination& destination
);
bool readDestination(
    binary::Reader& reader,
    modulation::ModulationDestination& destination
);
bool writeCurveRecord(
    binary::Writer& writer,
    const modulation::ProjectCurveRecord& record,
    uint16_t localPointOffset
);
bool writeCurvePoints(
    binary::Writer& writer,
    const modulation::ProjectCurveArena& arena,
    const modulation::ProjectCurveRecord& record
);
bool readZeroes(binary::Reader& reader, uint32_t count);
bool readCurveRecord(
    binary::Reader& reader,
    modulation::ProjectCurveRecord& record,
    modulation::ProjectCurveId id,
    uint16_t pointBase,
    uint16_t expectedLocalOffset,
    modulation::ProjectCurveValueDomain requiredDomain,
    bool acceptEitherDomain,
    bool requireNativeOrigin
);

bool automationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out
);
bool writeAutomationPayload(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t capacity,
    uint32_t expectedSize
);
ChunkStatus decodeAutomationCurrent(
    const ChunkPayloadView& view,
    modulation::ProjectControlDomainState& target
);

bool modulationLayout(
    const modulation::ProjectControlDomainState& source,
    PayloadLayout& out
);
bool writeModulationPayload(
    const modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t capacity,
    uint32_t expectedSize
);
ChunkStatus decodeModulationCurrent(
    const ChunkPayloadView& view,
    modulation::ProjectControlDomainState& target
);

}  // namespace core::persistence::project_control_codec::internal
