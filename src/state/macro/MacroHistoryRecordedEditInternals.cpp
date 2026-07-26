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

FLASHMEM bool recordedShapeEditStorageValid(
    const ProjectRecordedShapeEditHistoryPayload& payload
) {
    using namespace core::state::modulation;
    if (!payload.valid ||
        !payload.unrelatedGraphHashValid ||
        !payload.unrelatedCurveHashValid ||
        !valid(payload.beforeSource.id) ||
        payload.beforeSource.kind != ModulatorKind::RECORDED_SHAPE ||
        payload.afterSource.kind != ModulatorKind::RECORDED_SHAPE ||
        payload.beforeSource.id != payload.afterSource.id ||
        payload.beforeCurve.pointCount == 0U ||
        payload.afterCurve.pointCount == 0U ||
        payload.beforeCurve.pointCount >
            RECORDED_SHAPE_HISTORY_POINT_CAPACITY ||
        payload.afterCurve.pointCount >
            RECORDED_SHAPE_HISTORY_POINT_CAPACITY ||
        payload.beforePoints == nullptr || payload.afterPoints == nullptr ||
        (payload.boundaryPointCount > 0U &&
         payload.boundaryPoints == nullptr) ||
        payload.beforeCurveIndex >= payload.beforeRecordCount ||
        payload.afterCurveIndex >= payload.afterRecordCount ||
        payload.beforeRecordCount > PROJECT_CURVE_LIVE_CAPACITY ||
        payload.beforeRecordCount > PROJECT_CURVE_RECORD_CAPACITY ||
        payload.afterRecordCount > PROJECT_CURVE_LIVE_CAPACITY ||
        payload.afterRecordCount > PROJECT_CURVE_RECORD_CAPACITY ||
        payload.beforeArenaPointCount > PROJECT_CURVE_POINT_CAPACITY ||
        payload.afterArenaPointCount > PROJECT_CURVE_POINT_CAPACITY ||
        payload.beforeCurve.flags != 0U || payload.afterCurve.flags != 0U ||
        !validProjectCurveSpec(
            historyCurveSpec(payload.beforeCurve),
            payload.beforePoints.get(),
            payload.beforeCurve.pointCount
        ) || !validProjectCurveSpec(
            historyCurveSpec(payload.afterCurve),
            payload.afterPoints.get(),
            payload.afterCurve.pointCount
        ) ||
        static_cast<uint32_t>(payload.beforeCurve.pointOffset) +
                payload.beforeCurve.pointCount >
            payload.beforeArenaPointCount ||
        static_cast<uint32_t>(payload.afterCurve.pointOffset) +
                payload.afterCurve.pointCount >
            payload.afterArenaPointCount ||
        payload.beforeSource.parameters.recordedCurveId !=
            payload.beforeCurve.id ||
        payload.afterSource.parameters.recordedCurveId !=
            payload.afterCurve.id || !valid(payload.beforeCurve.id) ||
        !valid(payload.afterCurve.id)) {
        return false;
    }
    if (payload.copyOnWrite) {
        auto expectedPrevious = payload.beforeCurve;
        --expectedPrevious.referenceCount;
        auto expectedSource = payload.beforeSource;
        expectedSource.parameters.recordedCurveId = payload.afterCurve.id;
        return payload.beforeNextCurveId != 0U &&
            payload.afterCurve.id != payload.beforeCurve.id &&
            payload.afterCurve.id.value == payload.beforeNextCurveId &&
            payload.afterNextCurveId ==
                historyNextStableId(payload.beforeNextCurveId) &&
            payload.afterRecordCount == payload.beforeRecordCount + 1U &&
            payload.afterCurveIndex == payload.beforeRecordCount &&
            payload.afterCurve.pointOffset ==
                payload.beforeArenaPointCount &&
            payload.afterArenaPointCount ==
                payload.beforeArenaPointCount +
                    payload.afterCurve.pointCount &&
            payload.beforeCurve.id == payload.afterPreviousCurve.id &&
            payload.beforeCurve.referenceCount > 1U &&
            sameObjectBits(payload.afterPreviousCurve, expectedPrevious) &&
            sameObjectBits(payload.afterSource, expectedSource) &&
            payload.afterCurve.referenceCount == 1U &&
            payload.boundaryPointCount == payload.afterCurve.pointCount;
    }
    const uint16_t expectedBoundary = payload.beforeArenaPointCount >
            payload.afterArenaPointCount
        ? static_cast<uint16_t>(
              payload.beforeArenaPointCount - payload.afterArenaPointCount
          )
        : static_cast<uint16_t>(
              payload.afterArenaPointCount - payload.beforeArenaPointCount
          );
    return payload.beforeRecordCount == payload.afterRecordCount &&
        payload.beforeCurveIndex == payload.afterCurveIndex &&
        payload.beforeCurve.id == payload.afterCurve.id &&
        payload.beforeCurve.pointOffset == payload.afterCurve.pointOffset &&
        payload.afterArenaPointCount ==
            static_cast<uint32_t>(payload.beforeArenaPointCount) -
                payload.beforeCurve.pointCount +
                payload.afterCurve.pointCount &&
        payload.afterNextCurveId == payload.beforeNextCurveId &&
        sameObjectBits(payload.beforeSource, payload.afterSource) &&
        payload.beforeCurve.referenceCount == 1U &&
        payload.afterCurve.referenceCount == 1U &&
        payload.boundaryPointCount == expectedBoundary;
}

FLASHMEM bool recordedShapeEditMatches(
    const MacroPagesState& pages,
    const ProjectRecordedShapeEditHistoryPayload& payload,
    bool after
) {
    using namespace core::state::modulation;
    if (!recordedShapeEditStorageValid(payload)) return false;
    const auto& domain = pages.control.authored;
    const auto& graph = domain.modulation;
    const auto& arena = domain.curves;
    if (!historyDomainValid(domain)) return false;
    const auto* source = findProjectModulator(
        graph,
        payload.beforeSource.id
    );
    const auto graphHash = recordedShapeGraphHash(
        graph,
        payload.beforeSource.id
    );
    const auto curveHash = unrelatedCurveHash(
        domain,
        payload.beforeCurve.id,
        payload.copyOnWrite
            ? payload.afterCurve.id
            : ProjectCurveId{}
    );
    const auto& expectedSource = after
        ? payload.afterSource
        : payload.beforeSource;
    if (source == nullptr || !sameObjectBits(*source, expectedSource) ||
        !graphHash.valid || graphHash.value != payload.unrelatedGraphHash ||
        arena.nextCurveId !=
            (after ? payload.afterNextCurveId : payload.beforeNextCurveId) ||
        arena.recordCount !=
            (after ? payload.afterRecordCount : payload.beforeRecordCount) ||
        arena.pointCount != (after
            ? payload.afterArenaPointCount
            : payload.beforeArenaPointCount) ||
        !curveHash.valid || curveHash.value != payload.unrelatedCurveHash) {
        return false;
    }

    if (!after) {
        if (!sameObjectBits(
                arena.records[payload.beforeCurveIndex],
                payload.beforeCurve
            ) || !historyCurvePointsMatch(
                arena,
                payload.beforeCurve,
                payload.beforePoints.get(),
                payload.beforeCurve.pointCount
            )) {
            return false;
        }
        if (payload.copyOnWrite) {
            return sameObjectBits(
                    arena.records[payload.beforeRecordCount],
                    payload.beforeRecordTail
                ) && historyArenaRangeMatches(
                    arena,
                    payload.beforeArenaPointCount,
                    payload.boundaryPoints.get(),
                    payload.boundaryPointCount
                );
        }
        return payload.afterArenaPointCount <=
                   payload.beforeArenaPointCount ||
            historyArenaRangeMatches(
                arena,
                payload.beforeArenaPointCount,
                payload.boundaryPoints.get(),
                payload.boundaryPointCount
            );
    }

    if (payload.copyOnWrite &&
        (!sameObjectBits(
             arena.records[payload.beforeCurveIndex],
             payload.afterPreviousCurve
         ) || !historyCurvePointsMatch(
             arena,
             payload.afterPreviousCurve,
             payload.beforePoints.get(),
             payload.beforeCurve.pointCount
         ))) {
        return false;
    }
    if (!sameObjectBits(
            arena.records[payload.afterCurveIndex],
            payload.afterCurve
        ) || !historyCurvePointsMatch(
            arena,
            payload.afterCurve,
            payload.afterPoints.get(),
            payload.afterCurve.pointCount
        )) {
        return false;
    }
    return payload.copyOnWrite ||
        payload.afterArenaPointCount >= payload.beforeArenaPointCount ||
        historyArenaRangeMatches(
            arena,
            payload.afterArenaPointCount,
            payload.boundaryPoints.get(),
            payload.boundaryPointCount
        );
}

FLASHMEM bool applyRecordedShapeEdit(
    MacroPagesState& pages,
    const ProjectRecordedShapeEditHistoryPayload& payload,
    bool after
) {
    using namespace core::state::modulation;
    if (!recordedShapeEditMatches(pages, payload, !after)) return false;
    auto& domain = pages.control.authored;
    auto& graph = domain.modulation;
    auto& arena = domain.curves;
    auto* source = findProjectModulator(graph, payload.beforeSource.id);
    if (source == nullptr) return false;

    if (payload.copyOnWrite) {
        if (after) {
            arena.records[payload.beforeCurveIndex] =
                payload.afterPreviousCurve;
            arena.records[payload.afterCurveIndex] = payload.afterCurve;
            std::memcpy(
                arena.points.data() + payload.beforeArenaPointCount,
                payload.afterPoints.get(),
                static_cast<size_t>(payload.afterCurve.pointCount) *
                    sizeof(ProjectPackedCurvePoint)
            );
            arena.recordCount = payload.afterRecordCount;
            arena.pointCount = payload.afterArenaPointCount;
            arena.nextCurveId = payload.afterNextCurveId;
            *source = payload.afterSource;
        } else {
            *source = payload.beforeSource;
            arena.records[payload.beforeCurveIndex] = payload.beforeCurve;
            arena.records[payload.afterCurveIndex] =
                payload.beforeRecordTail;
            std::memcpy(
                arena.points.data() + payload.beforeArenaPointCount,
                payload.boundaryPoints.get(),
                static_cast<size_t>(payload.boundaryPointCount) *
                    sizeof(ProjectPackedCurvePoint)
            );
            arena.recordCount = payload.beforeRecordCount;
            arena.pointCount = payload.beforeArenaPointCount;
            arena.nextCurveId = payload.beforeNextCurveId;
        }
    } else {
        const auto& current = after
            ? payload.beforeCurve
            : payload.afterCurve;
        const auto& target = after ? payload.afterCurve : payload.beforeCurve;
        const auto* points = after
            ? payload.afterPoints.get()
            : payload.beforePoints.get();
        const uint16_t tailOffset = static_cast<uint16_t>(
            current.pointOffset + current.pointCount
        );
        const uint16_t tailCount = static_cast<uint16_t>(
            arena.pointCount - tailOffset
        );
        std::memmove(
            arena.points.data() + target.pointOffset + target.pointCount,
            arena.points.data() + tailOffset,
            static_cast<size_t>(tailCount) * sizeof(ProjectPackedCurvePoint)
        );
        const int32_t delta = static_cast<int32_t>(target.pointCount) -
            static_cast<int32_t>(current.pointCount);
        for (uint16_t index = 0U; index < arena.recordCount; ++index) {
            if (index != payload.beforeCurveIndex &&
                arena.records[index].pointOffset > current.pointOffset) {
                arena.records[index].pointOffset = static_cast<uint16_t>(
                    static_cast<int32_t>(arena.records[index].pointOffset) +
                    delta
                );
            }
        }
        std::memcpy(
            arena.points.data() + target.pointOffset,
            points,
            static_cast<size_t>(target.pointCount) *
                sizeof(ProjectPackedCurvePoint)
        );
        arena.records[payload.beforeCurveIndex] = target;
        arena.pointCount = after
            ? payload.afterArenaPointCount
            : payload.beforeArenaPointCount;
        arena.nextCurveId = after
            ? payload.afterNextCurveId
            : payload.beforeNextCurveId;
        *source = after ? payload.afterSource : payload.beforeSource;
        if ((!after && payload.afterArenaPointCount >
                           payload.beforeArenaPointCount) ||
            (after && payload.afterArenaPointCount <
                          payload.beforeArenaPointCount)) {
            const uint16_t offset = after
                ? payload.afterArenaPointCount
                : payload.beforeArenaPointCount;
            std::memcpy(
                arena.points.data() + offset,
                payload.boundaryPoints.get(),
                static_cast<size_t>(payload.boundaryPointCount) *
                    sizeof(ProjectPackedCurvePoint)
            );
        }
    }
    pages.control.markAuthoredMutation();
    return true;
}

FLASHMEM bool deleteAfterMatches(
    const MacroPagesState& pages,
    const ProjectModulatorDeleteHistoryPayload& payload
) {
    using namespace core::state::modulation;
    if ((payload.bindingCount > 0U && !payload.bindings) ||
        (payload.triggerCount > 0U && !payload.triggers) ||
        (payload.scaleCount > 0U && !payload.scales) ||
        (payload.curvePointCount > 0U && !payload.curvePoints)) {
        return false;
    }
    const auto& graph = pages.control.authored.modulation;
    if (findProjectModulator(graph, payload.source.id) != nullptr ||
        graph.sourceCount + 1U != payload.beforeSourceCount ||
        graph.outputBindingCount + payload.bindingCount !=
            payload.beforeBindingCount ||
        graph.triggerBindingCount + payload.triggerCount !=
            payload.beforeTriggerCount ||
        graph.destinationScaleCount + payload.scaleCount !=
            payload.beforeScaleCount ||
        graph.nextSourceId != payload.nextSourceId ||
        graph.nextBindingId != payload.nextBindingId ||
        unrelatedModulatorHash(graph, payload.source.id) !=
            payload.unrelatedHash) {
        return false;
    }
    for (uint16_t index = 0; index < payload.scaleCount; ++index) {
        if (findProjectModulationDestinationScale(
                graph,
                payload.scales[index].scale.destination
            ) != nullptr) {
            return false;
        }
    }
    if (!payload.curvePresent) return true;
    const auto& arena = pages.control.authored.curves;
    if (arena.nextCurveId != payload.nextCurveId) return false;
    const auto* record = findProjectCurve(arena, payload.curve.id);
    if (payload.curveShared) {
        if (record == nullptr || payload.curve.referenceCount <= 1U) return false;
        auto expected = payload.curve;
        --expected.referenceCount;
        return sameObjectBits(*record, expected);
    }
    return record == nullptr &&
           arena.recordCount + 1U == payload.beforeCurveRecordCount &&
           arena.pointCount + payload.curvePointCount ==
               payload.beforeCurveArenaPointCount;
}

}  // namespace history_detail

}  // namespace core::state::macro
