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
MacroHistoryService::splitProjectModulatorTrack(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    uint8_t track,
    const char* cloneName
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    failure.sourceId = sourceId;
    if (track >= PROJECT_MODULATION_TRACK_COUNT || cloneName == nullptr ||
        cloneName[0] == '\0') {
        return failure;
    }
    const auto& graph = pages.control.authored.modulation;
    uint16_t bindingCount = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId == sourceId &&
            binding.destination.track == track) {
            ++bindingCount;
        }
    }
    if (bindingCount == 0U) return failure;
    auto bindingIds = core::app::makeExtmemUniqueArrayForOverwrite<
        ModulationBindingId
    >(bindingCount);
    if (!bindingIds) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    uint16_t cursor = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId == sourceId &&
            binding.destination.track == track) {
            bindingIds[cursor++] = binding.id;
        }
    }
    const ModulatorSplitRequest request{
        .sourceId = sourceId,
        .cloneName = cloneName,
        .bindingIdsToMove = bindingIds.get(),
        .bindingCountToMove = bindingCount,
    };
    return splitProjectModulator(pages, request);
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::splitProjectModulator(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorSplitRequest& request
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    failure.sourceId = request.sourceId;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active() ||
        request.bindingIdsToMove == nullptr ||
        request.bindingCountToMove == 0U) {
        return failure;
    }

    auto& graph = pages.control.authored.modulation;
    auto& arena = pages.control.authored.curves;
    const auto* source = findProjectModulator(graph, request.sourceId);
    if (!source) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }
    if (graph.sourceCount >= graph.sources.size()) {
        failure.status = ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED;
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->modulatorSplit = core::app::makeExtmemUnique<
        ProjectModulatorSplitHistoryPayload
    >();
    if (!change->modulatorSplit) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::SPLIT_PROJECT_MODULATOR;
    auto& payload = *change->modulatorSplit;
    payload.retainedBefore = *source;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    payload.movedBindingCount = request.bindingCountToMove;
    while (payload.sourceIndex < graph.sourceCount &&
           graph.sources[payload.sourceIndex].id != request.sourceId) {
        ++payload.sourceIndex;
    }
    if (payload.sourceIndex >= graph.sourceCount) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }

    bool clonesTrigger = false;
    for (uint16_t index = 0; index < graph.triggerBindingCount; ++index) {
        if (graph.triggerBindings[index].sourceId == request.sourceId) {
            clonesTrigger = true;
            break;
        }
    }
    if (clonesTrigger) {
        if (graph.triggerBindingCount >= graph.triggerBindings.size()) {
            failure.status = ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED;
            return failure;
        }
        payload.beforeTriggerTail =
            graph.triggerBindings[graph.triggerBindingCount];
    }

    if (source->kind == ModulatorKind::RECORDED_SHAPE) {
        const auto* record = findProjectCurve(
            arena,
            source->parameters.recordedCurveId
        );
        if (!record) {
            failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
            return failure;
        }
        payload.sharedCurveReferenceCreated = true;
        payload.sharedCurveId = record->id;
        payload.beforeSharedCurveReferenceCount = record->referenceCount;
    }

    payload.movedBindings = core::app::makeExtmemUniqueArrayForOverwrite<
        ProjectModulatorSplitBindingEntry
    >(payload.movedBindingCount);
    if (!payload.movedBindings) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    for (uint16_t selected = 0;
         selected < payload.movedBindingCount;
         ++selected) {
        uint16_t index = 0;
        while (index < graph.outputBindingCount &&
               graph.outputBindings[index].id !=
                   request.bindingIdsToMove[selected]) {
            ++index;
        }
        if (index >= graph.outputBindingCount) {
            failure.status = ProjectModulationStatus::INVALID_ID;
            return failure;
        }
        payload.movedBindings[selected].before = graph.outputBindings[index];
        payload.movedBindings[selected].globalIndex = index;
    }

    const auto split = core::state::modulation::splitProjectModulator(
        graph,
        arena,
        request
    );
    if (!split.changed()) return split;

    payload.retainedAfter = graph.sources[payload.sourceIndex];
    payload.clone = graph.sources[payload.beforeSourceCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    payload.triggerCreated = graph.triggerBindingCount ==
        static_cast<uint16_t>(payload.beforeTriggerCount + 1U);
    if (payload.triggerCreated) {
        payload.cloneTrigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
    for (uint16_t selected = 0;
         selected < payload.movedBindingCount;
         ++selected) {
        auto& entry = payload.movedBindings[selected];
        entry.after = graph.outputBindings[entry.globalIndex];
    }

    pages.control.markAuthoredMutation();
    endCoalescing();
    recordNewEntry_(std::move(change));
    return split;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::deleteProjectModulator(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ID;
    failure.sourceId = sourceId;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active()) {
        failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
        return failure;
    }
    auto& graph = pages.control.authored.modulation;
    auto& arena = pages.control.authored.curves;
    const auto* source = findProjectModulator(graph, sourceId);
    if (!source) return failure;

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->modulatorDelete = core::app::makeExtmemUnique<
        ProjectModulatorDeleteHistoryPayload
    >();
    if (!change->modulatorDelete) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::DELETE_PROJECT_MODULATOR;
    auto& payload = *change->modulatorDelete;
    payload.source = *source;
    payload.nextSourceId = graph.nextSourceId;
    payload.nextBindingId = graph.nextBindingId;
    payload.nextCurveId = arena.nextCurveId;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeScaleCount = graph.destinationScaleCount;
    payload.beforeCurveRecordCount = arena.recordCount;
    payload.beforeCurveArenaPointCount = arena.pointCount;
    payload.unrelatedHash = unrelatedModulatorHash(graph, sourceId);
    while (payload.sourceIndex < graph.sourceCount &&
           graph.sources[payload.sourceIndex].id != sourceId) {
        ++payload.sourceIndex;
    }
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].sourceId == sourceId) {
            ++payload.bindingCount;
        }
    }
    for (uint16_t index = 0; index < graph.triggerBindingCount; ++index) {
        if (graph.triggerBindings[index].sourceId == sourceId) {
            ++payload.triggerCount;
        }
    }
    for (uint16_t index = 0; index < graph.destinationScaleCount; ++index) {
        if (destinationScaleRemovedWithSource(
                graph,
                graph.destinationScales[index].destination,
                sourceId
            )) {
            ++payload.scaleCount;
        }
    }
    if (payload.bindingCount > 0U) {
        payload.bindings = core::app::makeExtmemUniqueArrayForOverwrite<
            ProjectModulatorDeleteBindingEntry
        >(payload.bindingCount);
        if (!payload.bindings) {
            failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
            return failure;
        }
    }
    if (payload.triggerCount > 0U) {
        payload.triggers = core::app::makeExtmemUniqueArrayForOverwrite<
            ProjectModulatorDeleteTriggerEntry
        >(payload.triggerCount);
        if (!payload.triggers) {
            failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
            return failure;
        }
    }
    if (payload.scaleCount > 0U) {
        payload.scales = core::app::makeExtmemUniqueArrayForOverwrite<
            ProjectModulatorDeleteScaleEntry
        >(payload.scaleCount);
        if (!payload.scales) {
            failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
            return failure;
        }
    }
    uint16_t bindingCursor = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].sourceId != sourceId) continue;
        payload.bindings[bindingCursor++] = {
            .binding = graph.outputBindings[index],
            .globalIndex = index,
        };
    }
    uint16_t triggerCursor = 0;
    for (uint16_t index = 0; index < graph.triggerBindingCount; ++index) {
        if (graph.triggerBindings[index].sourceId != sourceId) continue;
        payload.triggers[triggerCursor++] = {
            .trigger = graph.triggerBindings[index],
            .globalIndex = index,
        };
    }
    uint16_t scaleCursor = 0;
    for (uint16_t index = 0; index < graph.destinationScaleCount; ++index) {
        const auto& scale = graph.destinationScales[index];
        if (!destinationScaleRemovedWithSource(
                graph,
                scale.destination,
                sourceId
            )) {
            continue;
        }
        payload.scales[scaleCursor++] = {.scale = scale};
    }

    if (source->kind == ModulatorKind::RECORDED_SHAPE) {
        const auto* record = findProjectCurve(
            arena,
            source->parameters.recordedCurveId
        );
        if (!record) {
            failure.status = ProjectModulationStatus::INVARIANT_VIOLATION;
            return failure;
        }
        payload.curvePresent = true;
        payload.curveShared = record->referenceCount > 1U;
        payload.curve = *record;
        while (payload.curveRecordIndex < arena.recordCount &&
               arena.records[payload.curveRecordIndex].id != record->id) {
            ++payload.curveRecordIndex;
        }
        if (!payload.curveShared) {
            if (record->pointCount > MACRO_HISTORY_POINT_CAPACITY) {
                failure.status =
                    ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
                return failure;
            }
            payload.curvePointCount = record->pointCount;
            if (payload.curvePointCount > 0U) {
                payload.curvePoints =
                    core::app::makeExtmemUniqueArrayForOverwrite<
                        ProjectPackedCurvePoint
                    >(payload.curvePointCount);
                if (!payload.curvePoints) {
                    failure.status =
                        ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
                    return failure;
                }
                std::memcpy(
                    payload.curvePoints.get(),
                    arena.points.data() + record->pointOffset,
                    static_cast<size_t>(record->pointCount) *
                        sizeof(ProjectPackedCurvePoint)
                );
            }
        }
    }

    const auto deleted = core::state::modulation::deleteProjectModulator(
        graph,
        arena,
        sourceId
    );
    if (!deleted.changed()) return deleted;
    pages.control.markAuthoredMutation();
    endCoalescing();
    recordNewEntry_(std::move(change));
    return deleted;
}

FLASHMEM void MacroHistoryService::endCoalescing() {
    coalescing_ = false;
}

}  // namespace core::state::macro
