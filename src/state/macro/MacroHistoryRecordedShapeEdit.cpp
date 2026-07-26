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

using namespace history_detail;

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::replaceProjectRecordedShapeCurve(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ProjectCurveSpec& curve,
    const core::state::modulation::ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    failure.sourceId = sourceId;
    if (!validProjectCurveSpec(curve, points, pointCount) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return failure;
    }
    auto& domain = pages.control.authored;
    auto& graph = domain.modulation;
    auto& arena = domain.curves;
    if (!historyDomainValid(domain)) {
        failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
        return failure;
    }
    const auto* source = findProjectModulator(graph, sourceId);
    if (source == nullptr) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }
    if (source->kind != ModulatorKind::RECORDED_SHAPE) return failure;
    const int16_t beforeIndex = historyCurveIndex(
        arena,
        source->parameters.recordedCurveId
    );
    if (beforeIndex < 0) {
        failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
        return failure;
    }
    const auto& beforeCurve = arena.records[static_cast<uint16_t>(beforeIndex)];
    if (beforeCurve.pointCount > RECORDED_SHAPE_HISTORY_POINT_CAPACITY ||
        pointCount > RECORDED_SHAPE_HISTORY_POINT_CAPACITY) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->recordedShapeEdit = core::app::makeExtmemUnique<
        ProjectRecordedShapeEditHistoryPayload
    >();
    if (!change->recordedShapeEdit) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    auto& payload = *change->recordedShapeEdit;
    payload.beforePoints = core::app::makeExtmemUniqueArrayForOverwrite<
        ProjectPackedCurvePoint
    >(beforeCurve.pointCount);
    payload.afterPoints = core::app::makeExtmemUniqueArrayForOverwrite<
        ProjectPackedCurvePoint
    >(pointCount);
    if (!payload.beforePoints || !payload.afterPoints) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }

    payload.beforeSource = *source;
    payload.beforeCurve = beforeCurve;
    payload.beforeNextCurveId = arena.nextCurveId;
    payload.beforeRecordCount = arena.recordCount;
    payload.beforeArenaPointCount = arena.pointCount;
    payload.beforeCurveIndex = static_cast<uint16_t>(beforeIndex);
    payload.copyOnWrite = beforeCurve.referenceCount > 1U;
    if (payload.copyOnWrite && arena.nextCurveId == 0U) {
        failure.status = ProjectModulationStatus::ID_EXHAUSTED;
        return failure;
    }
    const uint16_t projectedAfterPointCount = payload.copyOnWrite
        ? static_cast<uint16_t>(arena.pointCount + pointCount)
        : static_cast<uint16_t>(
              arena.pointCount - beforeCurve.pointCount + pointCount
          );
    payload.boundaryPointCount = payload.copyOnWrite
        ? pointCount
        : static_cast<uint16_t>(projectedAfterPointCount > arena.pointCount
              ? projectedAfterPointCount - arena.pointCount
              : arena.pointCount - projectedAfterPointCount);
    if (payload.boundaryPointCount > 0U) {
        payload.boundaryPoints =
            core::app::makeExtmemUniqueArrayForOverwrite<
                ProjectPackedCurvePoint
            >(payload.boundaryPointCount);
        if (!payload.boundaryPoints) {
            failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
            return failure;
        }
    }
    std::memcpy(
        payload.beforePoints.get(),
        arena.points.data() + beforeCurve.pointOffset,
        static_cast<size_t>(beforeCurve.pointCount) *
            sizeof(ProjectPackedCurvePoint)
    );
    std::memcpy(
        payload.afterPoints.get(),
        points,
        static_cast<size_t>(pointCount) * sizeof(ProjectPackedCurvePoint)
    );
    if (payload.copyOnWrite) {
        if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
            arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
            failure.status =
                ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED;
            return failure;
        }
        if (pointCount > PROJECT_CURVE_POINT_CAPACITY - arena.pointCount) {
            failure.status =
                ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED;
            return failure;
        }
        payload.beforeRecordTail = arena.records[arena.recordCount];
        std::memcpy(
            payload.boundaryPoints.get(),
            arena.points.data() + arena.pointCount,
            static_cast<size_t>(payload.boundaryPointCount) *
                sizeof(ProjectPackedCurvePoint)
        );
    } else {
        const uint32_t available = PROJECT_CURVE_POINT_CAPACITY -
            arena.pointCount + beforeCurve.pointCount;
        if (pointCount > available) {
            failure.status =
                ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED;
            return failure;
        }
        if (projectedAfterPointCount > arena.pointCount &&
            payload.boundaryPointCount > 0U) {
            std::memcpy(
                payload.boundaryPoints.get(),
                arena.points.data() + arena.pointCount,
                static_cast<size_t>(payload.boundaryPointCount) *
                    sizeof(ProjectPackedCurvePoint)
            );
        }
    }
    const ProjectCurveId projectedCurveId = payload.copyOnWrite
        ? ProjectCurveId{arena.nextCurveId}
        : ProjectCurveId{};
    const auto graphHash = recordedShapeGraphHash(graph, sourceId);
    const auto curveHash = unrelatedCurveHash(
        domain,
        beforeCurve.id,
        projectedCurveId
    );
    if (!graphHash.valid || !curveHash.valid) {
        failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
        return failure;
    }
    payload.unrelatedGraphHash = graphHash.value;
    payload.unrelatedCurveHash = curveHash.value;
    payload.unrelatedGraphHashValid = true;
    payload.unrelatedCurveHashValid = true;

    payload.afterSource = payload.beforeSource;
    payload.afterCurve = {};
    payload.afterCurve.id = payload.copyOnWrite
        ? projectedCurveId
        : payload.beforeCurve.id;
    payload.afterCurve.pointOffset = payload.copyOnWrite
        ? payload.beforeArenaPointCount
        : payload.beforeCurve.pointOffset;
    payload.afterCurve.pointCount = pointCount;
    payload.afterCurve.sourceDurationTicks = curve.sourceDurationTicks;
    payload.afterCurve.durationTicks = curve.durationTicks;
    payload.afterCurve.windowOffsetTicks = curve.windowOffsetTicks;
    payload.afterCurve.referenceCount = 1U;
    payload.afterCurve.interpolation = curve.interpolation;
    payload.afterCurve.valueDomain = curve.valueDomain;
    payload.afterCurve.origin = curve.origin;
    payload.afterRecordCount = payload.copyOnWrite
        ? static_cast<uint16_t>(payload.beforeRecordCount + 1U)
        : payload.beforeRecordCount;
    payload.afterArenaPointCount = projectedAfterPointCount;
    payload.afterCurveIndex = payload.copyOnWrite
        ? payload.beforeRecordCount
        : payload.beforeCurveIndex;
    payload.afterNextCurveId = payload.copyOnWrite
        ? historyNextStableId(payload.beforeNextCurveId)
        : payload.beforeNextCurveId;
    if (payload.copyOnWrite) {
        payload.afterPreviousCurve = payload.beforeCurve;
        --payload.afterPreviousCurve.referenceCount;
        payload.afterSource.parameters.recordedCurveId =
            payload.afterCurve.id;
    }
    payload.valid = true;
    if (!recordedShapeEditStorageValid(payload)) {
        failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
        return failure;
    }

    const auto replaced = core::state::modulation::replaceRecordedShapeCurve(
        graph,
        arena,
        sourceId,
        curve,
        payload.afterPoints.get(),
        pointCount
    );
    if (!replaced.changed()) return replaced;

    if (!payload.copyOnWrite && payload.afterArenaPointCount <
                   payload.beforeArenaPointCount &&
               payload.boundaryPointCount > 0U) {
        std::memcpy(
            payload.boundaryPoints.get(),
            arena.points.data() + payload.afterArenaPointCount,
            static_cast<size_t>(payload.boundaryPointCount) *
                sizeof(ProjectPackedCurvePoint)
        );
    }
    pages.control.markAuthoredMutation();
    endCoalescing();
    recordNewEntry_(std::move(change));
    return replaced;
}

FLASHMEM bool MacroHistoryService::setProjectModulationTriggerCoalesced(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationTriggerFilter& trigger,
    bool enabled,
    uint8_t velocityMin,
    uint8_t velocityMax
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        return false;
    }
    const auto* existing = findProjectModulationTriggerForSource(
        pages.control.authored.modulation,
        sourceId
    );
    if (existing == nullptr) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_TRIGGER_EDIT;
    change->triggerEdit.before = *existing;
    const auto result = core::state::modulation::setProjectModulationTrigger(
        pages.control.authored.modulation,
        sourceId,
        trigger,
        enabled,
        velocityMin,
        velocityMax
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->triggerEdit.after = *existing;
    change->triggerEdit.valid = true;

    if (coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == MacroHistoryActionKind::PROJECT_MODULATOR_TRIGGER_EDIT) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->triggerEdit.valid &&
            previous->triggerEdit.after.id == change->triggerEdit.before.id &&
            sameObjectBits(
                previous->triggerEdit.after,
                change->triggerEdit.before
            )) {
            previous->triggerEdit.after = change->triggerEdit.after;
            clearRedo_();
            return true;
        }
    }

    recordNewEntry_(std::move(change));
    coalescing_ = true;
    coalesced_kind_ = MacroHistoryActionKind::PROJECT_MODULATOR_TRIGGER_EDIT;
    return true;
}

}  // namespace core::state::macro
