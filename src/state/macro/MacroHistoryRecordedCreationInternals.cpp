#include "state/macro/MacroHistoryInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
namespace core::state::macro {

namespace history_detail {

FLASHMEM uint64_t hashBytes64(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

FLASHMEM bool historyGraphRangesValid(
    const core::state::modulation::ProjectModulationState& graph
) {
    return graph.sourceCount <= graph.sources.size() &&
        graph.outputBindingCount <= graph.outputBindings.size() &&
        graph.triggerBindingCount <= graph.triggerBindings.size() &&
        graph.destinationScaleCount <= graph.destinationScales.size();
}

FLASHMEM bool historyArenaRangesValid(
    const core::state::modulation::ProjectCurveArena& arena
) {
    using namespace core::state::modulation;
    if (arena.recordCount > PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount > arena.records.size() ||
        arena.pointCount > arena.points.size()) {
        return false;
    }
    uint32_t covered = 0U;
    for (uint16_t index = 0U; index < arena.recordCount; ++index) {
        const auto& record = arena.records[index];
        if (!valid(record.id) || record.pointCount == 0U ||
            static_cast<uint32_t>(record.pointOffset) + record.pointCount >
                arena.pointCount) {
            return false;
        }
        covered += record.pointCount;
        for (uint16_t prior = 0U; prior < index; ++prior) {
            const auto& previous = arena.records[prior];
            const uint32_t begin = record.pointOffset;
            const uint32_t end = begin + record.pointCount;
            const uint32_t previousBegin = previous.pointOffset;
            const uint32_t previousEnd = previousBegin + previous.pointCount;
            if (begin < previousEnd && previousBegin < end) return false;
        }
    }
    return covered == arena.pointCount;
}

FLASHMEM bool historyDomainValid(
    const core::state::modulation::ProjectControlDomainState& domain
) {
    return core::state::modulation::validProjectModulationDomain(
        domain.modulation,
        domain.curves,
        &domain.automation
    );
}

/** Excludes only the edited source record; every edge/trigger/scale is hashed. */
FLASHMEM HistoryHash64Result recordedShapeGraphHash(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
) {
    HistoryHash64Result out{};
    if (!historyGraphRangesValid(graph) ||
        !core::state::modulation::valid(sourceId)) {
        return out;
    }
    uint64_t hash = out.value;
    hash = hashBytes64(hash, &graph.nextSourceId, sizeof(graph.nextSourceId));
    hash = hashBytes64(hash, &graph.nextBindingId, sizeof(graph.nextBindingId));
    hash = hashBytes64(hash, &graph.sourceCount, sizeof(graph.sourceCount));
    hash = hashBytes64(
        hash,
        &graph.outputBindingCount,
        sizeof(graph.outputBindingCount)
    );
    hash = hashBytes64(
        hash,
        &graph.triggerBindingCount,
        sizeof(graph.triggerBindingCount)
    );
    hash = hashBytes64(
        hash,
        &graph.destinationScaleCount,
        sizeof(graph.destinationScaleCount)
    );
    bool excluded = false;
    for (uint16_t index = 0U; index < graph.sourceCount; ++index) {
        if (graph.sources[index].id == sourceId) {
            if (excluded) return out;
            excluded = true;
            continue;
        }
        hash = hashBytes64(hash, &graph.sources[index], sizeof(graph.sources[index]));
    }
    if (!excluded) return out;
    hash = hashBytes64(
        hash,
        graph.outputBindings.data(),
        static_cast<size_t>(graph.outputBindingCount) *
            sizeof(graph.outputBindings[0])
    );
    hash = hashBytes64(
        hash,
        graph.triggerBindings.data(),
        static_cast<size_t>(graph.triggerBindingCount) *
            sizeof(graph.triggerBindings[0])
    );
    hash = hashBytes64(
        hash,
        graph.destinationScales.data(),
        static_cast<size_t>(graph.destinationScaleCount) *
            sizeof(graph.destinationScales[0])
    );
    out.value = hash;
    out.valid = true;
    return out;
}

FLASHMEM int16_t historyCurveIndex(
    const core::state::modulation::ProjectCurveArena& arena,
    core::state::modulation::ProjectCurveId id
) {
    if (!historyArenaRangesValid(arena) ||
        !core::state::modulation::valid(id)) {
        return -1;
    }
    for (uint16_t index = 0U; index < arena.recordCount; ++index) {
        if (arena.records[index].id == id) {
            return static_cast<int16_t>(index);
        }
    }
    return -1;
}

FLASHMEM core::state::modulation::ProjectCurveSpec historyCurveSpec(
    const core::state::modulation::ProjectCurveRecord& record
) {
    return {
        .sourceDurationTicks = record.sourceDurationTicks,
        .durationTicks = record.durationTicks,
        .windowOffsetTicks = record.windowOffsetTicks,
        .interpolation = record.interpolation,
        .valueDomain = record.valueDomain,
        .origin = record.origin,
    };
}

FLASHMEM uint32_t historyNextStableId(uint32_t current) {
    return current == std::numeric_limits<uint32_t>::max()
        ? 0U
        : current + 1U;
}

FLASHMEM bool historyCurvePointsMatch(
    const core::state::modulation::ProjectCurveArena& arena,
    const core::state::modulation::ProjectCurveRecord& record,
    const core::state::modulation::ProjectPackedCurvePoint* expected,
    uint16_t count
) {
    return expected != nullptr && record.pointCount == count &&
        static_cast<uint32_t>(record.pointOffset) + count <= arena.pointCount &&
        std::memcmp(
            arena.points.data() + record.pointOffset,
            expected,
            static_cast<size_t>(count) *
                sizeof(core::state::modulation::ProjectPackedCurvePoint)
        ) == 0;
}

FLASHMEM bool historyArenaRangeMatches(
    const core::state::modulation::ProjectCurveArena& arena,
    uint16_t offset,
    const core::state::modulation::ProjectPackedCurvePoint* expected,
    uint16_t count
) {
    return count == 0U ||
        (expected != nullptr &&
         static_cast<uint32_t>(offset) + count <= arena.points.size() &&
         std::memcmp(
             arena.points.data() + offset,
             expected,
             static_cast<size_t>(count) *
                 sizeof(core::state::modulation::ProjectPackedCurvePoint)
         ) == 0);
}

/** Hashes semantic content outside one or two edited curves. */
FLASHMEM HistoryHash64Result unrelatedCurveHash(
    const core::state::modulation::ProjectControlDomainState& domain,
    core::state::modulation::ProjectCurveId excludedA,
    core::state::modulation::ProjectCurveId excludedB
) {
    using namespace core::state::modulation;
    HistoryHash64Result out{};
    if (!historyArenaRangesValid(domain.curves) ||
        domain.automation.entryCount > domain.automation.entries.size() ||
        domain.automation.reserved != 0U) {
        return out;
    }
    uint64_t hash = out.value;
    uint16_t retained = 0U;
    for (uint16_t index = 0U; index < domain.curves.recordCount; ++index) {
        const auto& live = domain.curves.records[index];
        if (live.id == excludedA ||
            (valid(excludedB) && live.id == excludedB)) {
            continue;
        }
        auto semantic = live;
        semantic.pointOffset = 0U;
        hash = hashBytes64(hash, &semantic, sizeof(semantic));
        hash = hashBytes64(
            hash,
            domain.curves.points.data() + live.pointOffset,
            static_cast<size_t>(live.pointCount) *
                sizeof(ProjectPackedCurvePoint)
        );
        ++retained;
    }
    hash = hashBytes64(hash, &retained, sizeof(retained));
    out.value = hashBytes64(
        hash,
        &domain.automation,
        sizeof(domain.automation)
    );
    out.valid = true;
    return out;
}

FLASHMEM bool recordedCreationStorageValid(
    const ProjectRecordedShapeCreationHistoryPayload& payload
) {
    using namespace core::state::modulation;
    return payload.valid && payload.pointCount > 0U &&
        payload.pointCount <= RECORDED_SHAPE_HISTORY_POINT_CAPACITY &&
        payload.points != nullptr && payload.beforePointTail != nullptr &&
        payload.unrelatedCurveHashValid &&
        payload.beforeNextCurveId != 0U &&
        payload.curve.id.value == payload.beforeNextCurveId &&
        payload.afterNextCurveId ==
            historyNextStableId(payload.beforeNextCurveId) &&
        payload.beforeRecordCount < PROJECT_CURVE_RECORD_CAPACITY &&
        payload.beforeRecordCount < PROJECT_CURVE_LIVE_CAPACITY &&
        static_cast<uint32_t>(payload.beforePointCount) + payload.pointCount <=
            PROJECT_CURVE_POINT_CAPACITY &&
        payload.curve.pointOffset == payload.beforePointCount &&
        payload.curve.pointCount == payload.pointCount &&
        payload.curve.referenceCount == 1U && payload.curve.flags == 0U &&
        valid(payload.curve.id) && validProjectCurveSpec(
            historyCurveSpec(payload.curve),
            payload.points.get(),
            payload.pointCount
        );
}

FLASHMEM bool recordedCreationMatches(
    const core::state::modulation::ProjectControlState& control,
    const ProjectRecordedShapeCreationHistoryPayload& payload,
    bool after
) {
    using namespace core::state::modulation;
    if (!recordedCreationStorageValid(payload)) return false;
    const auto& domain = control.authored;
    const auto& arena = domain.curves;
    if (!historyDomainValid(domain)) return false;
    const auto curveHash = unrelatedCurveHash(domain, payload.curve.id);
    if (!curveHash.valid || curveHash.value != payload.unrelatedCurveHash) {
        return false;
    }
    if (after) {
        if (arena.nextCurveId != payload.afterNextCurveId ||
            arena.recordCount != payload.beforeRecordCount + 1U ||
            arena.pointCount != payload.beforePointCount + payload.pointCount ||
            !sameObjectBits(
                arena.records[payload.beforeRecordCount],
                payload.curve
            )) {
            return false;
        }
        return historyCurvePointsMatch(
            arena,
            payload.curve,
            payload.points.get(),
            payload.pointCount
        );
    }
    return arena.nextCurveId == payload.beforeNextCurveId &&
        arena.recordCount == payload.beforeRecordCount &&
        arena.pointCount == payload.beforePointCount &&
        sameObjectBits(
            arena.records[payload.beforeRecordCount],
            payload.beforeRecordTail
        ) && historyArenaRangeMatches(
            arena,
            payload.beforePointCount,
            payload.beforePointTail.get(),
            payload.pointCount
        );
}

FLASHMEM void restoreRecordedCreation(
    core::state::modulation::ProjectControlState& control,
    const ProjectRecordedShapeCreationHistoryPayload& payload,
    bool after
) {
    auto& arena = control.authored.curves;
    if (after) {
        arena.records[payload.beforeRecordCount] = payload.curve;
        std::memcpy(
            arena.points.data() + payload.beforePointCount,
            payload.points.get(),
            static_cast<size_t>(payload.pointCount) *
                sizeof(core::state::modulation::ProjectPackedCurvePoint)
        );
        arena.recordCount = static_cast<uint16_t>(
            payload.beforeRecordCount + 1U
        );
        arena.pointCount = static_cast<uint16_t>(
            payload.beforePointCount + payload.pointCount
        );
        arena.nextCurveId = payload.afterNextCurveId;
        return;
    }
    arena.records[payload.beforeRecordCount] = payload.beforeRecordTail;
    std::memcpy(
        arena.points.data() + payload.beforePointCount,
        payload.beforePointTail.get(),
        static_cast<size_t>(payload.pointCount) *
            sizeof(core::state::modulation::ProjectPackedCurvePoint)
    );
    arena.recordCount = payload.beforeRecordCount;
    arena.pointCount = payload.beforePointCount;
    arena.nextCurveId = payload.beforeNextCurveId;
}

}  // namespace history_detail

}  // namespace core::state::macro
