#include "state/macro/MacroHistory.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::macro {

namespace {

FLASHMEM bool sameCurveMetadata(
    const MacroAutomationCurveRef& lhs,
    const MacroAutomationCurveRef& rhs
) {
    return lhs.active == rhs.active &&
           lhs.playbackState == rhs.playbackState &&
           lhs.pointCount == rhs.pointCount &&
           lhs.sourceDurationTicks == rhs.sourceDurationTicks &&
           lhs.durationTicks == rhs.durationTicks &&
           lhs.windowOffsetTicks == rhs.windowOffsetTicks &&
           lhs.interpolation == rhs.interpolation &&
           lhs.modulationOrigin == rhs.modulationOrigin;
}

FLASHMEM bool sameFloatBits(float lhs, float rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
}

FLASHMEM bool samePoint(
    const MacroPackedCurvePoint& lhs,
    const MacroPackedCurvePoint& rhs
) {
    return lhs.tick == rhs.tick && lhs.value == rhs.value;
}

FLASHMEM uint16_t snapshotPointCount(const MacroSlotHistorySnapshot& snapshot) {
    return static_cast<uint16_t>(
        snapshot.automationPointCount + snapshot.modulationPointCount
    );
}

FLASHMEM bool snapshotConsistent(const MacroSlotHistorySnapshot& snapshot) {
    if (!macroAutomationAddressValid(snapshot.address)) return false;
    const uint32_t total = static_cast<uint32_t>(snapshot.automationPointCount) +
                           snapshot.modulationPointCount;
    if (total > MACRO_HISTORY_POINT_CAPACITY) return false;
    if (!snapshot.slotPresent) {
        return snapshot.automationPointCount == 0 &&
               snapshot.modulationPointCount == 0;
    }
    if (snapshot.slot.automation.active != (snapshot.automationPointCount > 0) ||
        snapshot.slot.modulation.active != (snapshot.modulationPointCount > 0) ||
        snapshot.slot.automation.pointCount != snapshot.automationPointCount ||
        snapshot.slot.modulation.pointCount != snapshot.modulationPointCount) {
        return false;
    }
    if (snapshot.slot.automation.active && snapshot.slot.automation.pointOffset != 0) {
        return false;
    }
    return !snapshot.slot.modulation.active ||
           snapshot.slot.modulation.pointOffset == snapshot.automationPointCount;
}

FLASHMEM bool automationSnapshotConsistent(
    const MacroAutomationHistorySnapshot& snapshot
) {
    if (!macroAutomationAddressValid(snapshot.address) ||
        !macroAutomationCurveLifecycleValid(snapshot.automation) ||
        snapshot.pointCount > MACRO_AUTOMATION_RECORDING_MAX_POINTS ||
        snapshot.automation.active != (snapshot.pointCount > 0U) ||
        snapshot.automation.pointCount != snapshot.pointCount ||
        (snapshot.pointCount > 0U && !snapshot.points) ||
        (snapshot.pointCount > 0U &&
         snapshot.automation.pointOffset != 0U)) {
        return false;
    }
    return true;
}

FLASHMEM void normalizeCurveOffsets(MacroSlotHistorySnapshot& snapshot) {
    snapshot.slot.automation.pointOffset = 0;
    snapshot.slot.modulation.pointOffset = snapshot.automationPointCount;
}

FLASHMEM bool liveProjectCurveMatches(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ProjectCurveId curveId,
    const MacroAutomationCurveRef& live,
    const MacroAutomationCurveRef& expected,
    const MacroSlotHistorySnapshot& snapshot,
    uint16_t snapshotOffset
) {
    if (!sameCurveMetadata(live, expected)) return false;
    if (!live.active) return !core::state::modulation::valid(curveId);
    const auto* record = core::state::modulation::findProjectCurve(
        control.authored.curves,
        curveId
    );
    if (record == nullptr || record->pointCount != live.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            control.authored.curves.pointCount ||
        static_cast<uint32_t>(snapshotOffset) + live.pointCount >
            snapshot.points.size()) {
        return false;
    }
    for (uint16_t i = 0; i < live.pointCount; ++i) {
        const auto& point = control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + i)
        ];
        const MacroPackedCurvePoint livePoint{point.tick, point.value};
        if (!samePoint(
                livePoint,
                snapshot.points[static_cast<uint16_t>(snapshotOffset + i)]
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sameAddress(
    const MacroAutomationSlotAddress& lhs,
    const MacroAutomationSlotAddress& rhs
) {
    return macroAutomationAddressEquals(lhs, rhs);
}

template <typename T>
FLASHMEM bool sameObjectBits(const T& lhs, const T& rhs) {
    static_assert(std::is_trivially_copyable_v<T>);
    return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

FLASHMEM uint32_t hashBytes(uint32_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }
    return hash;
}

FLASHMEM uint32_t unrelatedModulatorHash(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
) {
    uint32_t hash = 2166136261UL;
    hash = hashBytes(hash, &graph.nextSourceId, sizeof(graph.nextSourceId));
    hash = hashBytes(hash, &graph.nextBindingId, sizeof(graph.nextBindingId));
    for (uint16_t index = 0; index < graph.sourceCount; ++index) {
        if (graph.sources[index].id == sourceId) continue;
        hash = hashBytes(hash, &graph.sources[index], sizeof(graph.sources[index]));
    }
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].sourceId == sourceId) continue;
        hash = hashBytes(
            hash,
            &graph.outputBindings[index],
            sizeof(graph.outputBindings[index])
        );
    }
    for (uint16_t index = 0; index < graph.triggerBindingCount; ++index) {
        if (graph.triggerBindings[index].sourceId == sourceId) continue;
        hash = hashBytes(
            hash,
            &graph.triggerBindings[index],
            sizeof(graph.triggerBindings[index])
        );
    }
    return hash;
}

template <typename Entry, size_t Capacity>
FLASHMEM bool insertDenseHistoryEntry(
    std::array<Entry, Capacity>& entries,
    uint16_t& count,
    uint16_t index,
    const Entry& value
) {
    if (count >= Capacity || index > count) return false;
    for (uint16_t cursor = count; cursor > index; --cursor) {
        entries[cursor] = entries[cursor - 1U];
    }
    entries[index] = value;
    ++count;
    return true;
}

FLASHMEM bool deleteAfterMatches(
    const MacroPagesState& pages,
    const ProjectModulatorDeleteHistoryPayload& payload
) {
    using namespace core::state::modulation;
    if ((payload.bindingCount > 0U && !payload.bindings) ||
        (payload.triggerCount > 0U && !payload.triggers) ||
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
        graph.nextSourceId != payload.nextSourceId ||
        graph.nextBindingId != payload.nextBindingId ||
        unrelatedModulatorHash(graph, payload.source.id) !=
            payload.unrelatedHash) {
        return false;
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

FLASHMEM bool deleteBeforeMatches(
    const MacroPagesState& pages,
    const ProjectModulatorDeleteHistoryPayload& payload
) {
    using namespace core::state::modulation;
    if ((payload.bindingCount > 0U && !payload.bindings) ||
        (payload.triggerCount > 0U && !payload.triggers) ||
        (payload.curvePointCount > 0U && !payload.curvePoints)) {
        return false;
    }
    const auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount != payload.beforeSourceCount ||
        graph.outputBindingCount != payload.beforeBindingCount ||
        graph.triggerBindingCount != payload.beforeTriggerCount ||
        graph.nextSourceId != payload.nextSourceId ||
        graph.nextBindingId != payload.nextBindingId ||
        payload.sourceIndex >= graph.sourceCount ||
        !sameObjectBits(graph.sources[payload.sourceIndex], payload.source) ||
        unrelatedModulatorHash(graph, payload.source.id) !=
            payload.unrelatedHash) {
        return false;
    }
    for (uint16_t index = 0; index < payload.bindingCount; ++index) {
        const auto& entry = payload.bindings[index];
        if (entry.globalIndex >= graph.outputBindingCount ||
            !sameObjectBits(
                graph.outputBindings[entry.globalIndex],
                entry.binding
            )) {
            return false;
        }
    }
    for (uint16_t index = 0; index < payload.triggerCount; ++index) {
        const auto& entry = payload.triggers[index];
        if (entry.globalIndex >= graph.triggerBindingCount ||
            !sameObjectBits(
                graph.triggerBindings[entry.globalIndex],
                entry.trigger
            )) {
            return false;
        }
    }
    if (!payload.curvePresent) return true;
    const auto& arena = pages.control.authored.curves;
    const auto* record = findProjectCurve(arena, payload.curve.id);
    return arena.nextCurveId == payload.nextCurveId &&
           arena.recordCount == payload.beforeCurveRecordCount &&
           arena.pointCount == payload.beforeCurveArenaPointCount &&
           record != nullptr && sameObjectBits(*record, payload.curve);
}

FLASHMEM bool restoreDeletedModulator(
    MacroPagesState& pages,
    const ProjectModulatorDeleteHistoryPayload& payload
) {
    using namespace core::state::modulation;
    if (!deleteAfterMatches(pages, payload)) return false;
    auto& graph = pages.control.authored.modulation;
    auto& arena = pages.control.authored.curves;

    if (payload.curvePresent) {
        if (payload.curveShared) {
            auto* record = const_cast<ProjectCurveRecord*>(
                findProjectCurve(arena, payload.curve.id)
            );
            if (!record) return false;
            ++record->referenceCount;
        } else {
            if (arena.recordCount >= arena.records.size() ||
                payload.curveRecordIndex > arena.recordCount ||
                static_cast<uint32_t>(arena.pointCount) +
                        payload.curvePointCount >
                    arena.points.size() ||
                payload.curve.pointOffset > arena.pointCount) {
                return false;
            }
            const uint16_t pointTail = static_cast<uint16_t>(
                arena.pointCount - payload.curve.pointOffset
            );
            std::memmove(
                arena.points.data() + payload.curve.pointOffset +
                    payload.curvePointCount,
                arena.points.data() + payload.curve.pointOffset,
                static_cast<size_t>(pointTail) * sizeof(ProjectPackedCurvePoint)
            );
            if (payload.curvePointCount > 0U) {
                std::memcpy(
                    arena.points.data() + payload.curve.pointOffset,
                    payload.curvePoints.get(),
                    static_cast<size_t>(payload.curvePointCount) *
                        sizeof(ProjectPackedCurvePoint)
                );
            }
            for (uint16_t index = 0; index < arena.recordCount; ++index) {
                if (arena.records[index].pointOffset >= payload.curve.pointOffset) {
                    arena.records[index].pointOffset = static_cast<uint16_t>(
                        arena.records[index].pointOffset + payload.curvePointCount
                    );
                }
            }
            for (uint16_t cursor = arena.recordCount;
                 cursor > payload.curveRecordIndex;
                 --cursor) {
                arena.records[cursor] = arena.records[cursor - 1U];
            }
            arena.records[payload.curveRecordIndex] = payload.curve;
            ++arena.recordCount;
            arena.pointCount = static_cast<uint16_t>(
                arena.pointCount + payload.curvePointCount
            );
        }
    }

    if (!insertDenseHistoryEntry(
            graph.sources,
            graph.sourceCount,
            payload.sourceIndex,
            payload.source
        )) {
        return false;
    }
    for (uint16_t index = 0; index < payload.bindingCount; ++index) {
        const auto& entry = payload.bindings[index];
        if (!insertDenseHistoryEntry(
                graph.outputBindings,
                graph.outputBindingCount,
                entry.globalIndex,
                entry.binding
            )) {
            return false;
        }
    }
    for (uint16_t index = 0; index < payload.triggerCount; ++index) {
        const auto& entry = payload.triggers[index];
        if (!insertDenseHistoryEntry(
                graph.triggerBindings,
                graph.triggerBindingCount,
                entry.globalIndex,
                entry.trigger
            )) {
            return false;
        }
    }
    pages.control.markAuthoredMutation();
    return true;
}

FLASHMEM bool creationIdentityMatches(
    const core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactAfter
) {
    const auto& graph = control.authored.modulation;
    const uint16_t expectedSourceCount = static_cast<uint16_t>(
        payload.beforeSourceCount + (payload.sourceCreated ? 1U : 0U)
    );
    const uint16_t expectedBindingCount = static_cast<uint16_t>(
        payload.beforeBindingCount + (payload.bindingCreated ? 1U : 0U)
    );
    if (graph.sourceCount != expectedSourceCount ||
        graph.outputBindingCount != expectedBindingCount ||
        graph.nextSourceId != payload.afterNextSourceId ||
        graph.nextBindingId != payload.afterNextBindingId) {
        return false;
    }
    const auto* source = core::state::modulation::findProjectModulator(
        graph,
        payload.source.id
    );
    if (source == nullptr) {
        return false;
    }
    if (payload.bindingCreated) {
        const auto& binding = graph.outputBindings[payload.beforeBindingCount];
        if (binding.id != payload.binding.id ||
            binding.sourceId != source->id ||
            binding.destination != payload.binding.destination ||
            (exactAfter && !sameObjectBits(binding, payload.binding))) {
            return false;
        }
    }
    if (payload.sharedCurveReferenceCreated) {
        const auto* record = core::state::modulation::findProjectCurve(
            control.authored.curves,
            payload.sharedCurveId
        );
        if (record == nullptr ||
            record->referenceCount !=
                payload.beforeSharedCurveReferenceCount + 1U) {
            return false;
        }
    }
    return !exactAfter || sameObjectBits(*source, payload.source);
}

FLASHMEM bool creationBeforeMatches(
    const core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload
) {
    const auto& graph = control.authored.modulation;
    if (graph.sourceCount != payload.beforeSourceCount ||
        graph.outputBindingCount != payload.beforeBindingCount ||
        graph.nextSourceId != payload.beforeNextSourceId ||
        graph.nextBindingId != payload.beforeNextBindingId ||
        (payload.bindingCreated &&
         !sameObjectBits(
             graph.outputBindings[payload.beforeBindingCount],
             payload.beforeBindingTail
         ))) {
        return false;
    }
    if (payload.sourceCreated) {
        if (payload.beforeSourceCount >= graph.sources.size() ||
            !sameObjectBits(
                graph.sources[payload.beforeSourceCount],
                payload.beforeSourceTail
            )) {
            return false;
        }
        if (payload.sharedCurveReferenceCreated) {
            const auto* record = core::state::modulation::findProjectCurve(
                control.authored.curves,
                payload.sharedCurveId
            );
            return record != nullptr &&
                   record->referenceCount ==
                       payload.beforeSharedCurveReferenceCount;
        }
        return true;
    }
    const auto* source = core::state::modulation::findProjectModulator(
        graph,
        payload.source.id
    );
    return source != nullptr && sameObjectBits(*source, payload.beforeSource);
}

FLASHMEM void restoreCreationBefore(
    core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactCancel
) {
    auto& graph = control.authored.modulation;
    if (payload.sourceCreated) {
        graph.sources[payload.beforeSourceCount] = payload.beforeSourceTail;
        graph.sourceCount = payload.beforeSourceCount;
    } else {
        auto* source = core::state::modulation::findProjectModulator(
            graph,
            payload.beforeSource.id
        );
        if (source != nullptr) *source = payload.beforeSource;
    }
    graph.nextSourceId = payload.beforeNextSourceId;
    if (payload.sharedCurveReferenceCreated) {
        auto* record = const_cast<core::state::modulation::ProjectCurveRecord*>(
            core::state::modulation::findProjectCurve(
                control.authored.curves,
                payload.sharedCurveId
            )
        );
        if (record != nullptr && record->referenceCount > 0U) {
            --record->referenceCount;
        }
    }
    if (payload.bindingCreated) {
        graph.outputBindings[payload.beforeBindingCount] =
            payload.beforeBindingTail;
        graph.outputBindingCount = payload.beforeBindingCount;
    }
    graph.nextBindingId = payload.beforeNextBindingId;
    if (exactCancel) {
        control.authoredRevision = payload.beforeAuthoredRevision;
    } else {
        control.markAuthoredMutation();
    }
}

FLASHMEM void restoreCreationAfter(
    core::state::modulation::ProjectControlState& control,
    const MacroModulatorCreationHistoryPayload& payload
) {
    auto& graph = control.authored.modulation;
    if (payload.sourceCreated) {
        if (payload.sharedCurveReferenceCreated) {
            auto* record = const_cast<
                core::state::modulation::ProjectCurveRecord*
            >(core::state::modulation::findProjectCurve(
                control.authored.curves,
                payload.sharedCurveId
            ));
            if (record != nullptr) ++record->referenceCount;
        }
        graph.sources[payload.beforeSourceCount] = payload.source;
        graph.sourceCount = static_cast<uint16_t>(
            payload.beforeSourceCount + 1U
        );
    } else {
        auto* source = core::state::modulation::findProjectModulator(
            graph,
            payload.source.id
        );
        if (source != nullptr) *source = payload.source;
    }
    graph.nextSourceId = payload.afterNextSourceId;
    if (payload.bindingCreated) {
        graph.outputBindings[payload.beforeBindingCount] = payload.binding;
        graph.outputBindingCount = static_cast<uint16_t>(
            payload.beforeBindingCount + 1U
        );
    }
    graph.nextBindingId = payload.afterNextBindingId;
    control.markAuthoredMutation();
}

FLASHMEM bool splitPayloadStorageValid(
    const ProjectModulatorSplitHistoryPayload& payload
) {
    return payload.movedBindingCount > 0U && payload.movedBindings != nullptr;
}

FLASHMEM bool splitCurveReferenceMatches(
    const core::state::modulation::ProjectControlState& control,
    const ProjectModulatorSplitHistoryPayload& payload,
    bool after
) {
    if (!payload.sharedCurveReferenceCreated) return true;
    const auto* record = core::state::modulation::findProjectCurve(
        control.authored.curves,
        payload.sharedCurveId
    );
    return record != nullptr &&
           record->referenceCount == static_cast<uint16_t>(
               payload.beforeSharedCurveReferenceCount + (after ? 1U : 0U)
           );
}

FLASHMEM bool splitBeforeMatches(
    const MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
) {
    if (!splitPayloadStorageValid(payload)) return false;
    const auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount != payload.beforeSourceCount ||
        graph.outputBindingCount != payload.beforeBindingCount ||
        graph.triggerBindingCount != payload.beforeTriggerCount ||
        graph.nextSourceId != payload.beforeNextSourceId ||
        graph.nextBindingId != payload.beforeNextBindingId ||
        payload.sourceIndex >= graph.sourceCount ||
        payload.beforeSourceCount >= graph.sources.size() ||
        !sameObjectBits(
            graph.sources[payload.sourceIndex],
            payload.retainedBefore
        ) ||
        !sameObjectBits(
            graph.sources[payload.beforeSourceCount],
            payload.beforeSourceTail
        )) {
        return false;
    }
    for (uint16_t index = 0; index < payload.movedBindingCount; ++index) {
        const auto& entry = payload.movedBindings[index];
        if (entry.globalIndex >= graph.outputBindingCount ||
            !sameObjectBits(
                graph.outputBindings[entry.globalIndex],
                entry.before
            )) {
            return false;
        }
    }
    if (payload.triggerCreated &&
        (payload.beforeTriggerCount >= graph.triggerBindings.size() ||
         !sameObjectBits(
             graph.triggerBindings[payload.beforeTriggerCount],
             payload.beforeTriggerTail
         ))) {
        return false;
    }
    return splitCurveReferenceMatches(pages.control, payload, false);
}

FLASHMEM bool splitAfterMatches(
    const MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
) {
    if (!splitPayloadStorageValid(payload)) return false;
    const auto& graph = pages.control.authored.modulation;
    const uint16_t expectedTriggerCount = static_cast<uint16_t>(
        payload.beforeTriggerCount + (payload.triggerCreated ? 1U : 0U)
    );
    if (graph.sourceCount != payload.beforeSourceCount + 1U ||
        graph.outputBindingCount != payload.beforeBindingCount ||
        graph.triggerBindingCount != expectedTriggerCount ||
        graph.nextSourceId != payload.afterNextSourceId ||
        graph.nextBindingId != payload.afterNextBindingId ||
        payload.sourceIndex >= payload.beforeSourceCount ||
        !sameObjectBits(
            graph.sources[payload.sourceIndex],
            payload.retainedAfter
        ) ||
        !sameObjectBits(
            graph.sources[payload.beforeSourceCount],
            payload.clone
        )) {
        return false;
    }
    for (uint16_t index = 0; index < payload.movedBindingCount; ++index) {
        const auto& entry = payload.movedBindings[index];
        if (entry.globalIndex >= graph.outputBindingCount ||
            !sameObjectBits(
                graph.outputBindings[entry.globalIndex],
                entry.after
            )) {
            return false;
        }
    }
    if (payload.triggerCreated &&
        !sameObjectBits(
            graph.triggerBindings[payload.beforeTriggerCount],
            payload.cloneTrigger
        )) {
        return false;
    }
    return splitCurveReferenceMatches(pages.control, payload, true);
}

FLASHMEM bool restoreSplitBefore(
    MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
) {
    if (!splitAfterMatches(pages, payload)) return false;
    auto& graph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < payload.movedBindingCount; ++index) {
        const auto& entry = payload.movedBindings[index];
        graph.outputBindings[entry.globalIndex] = entry.before;
    }
    graph.sources[payload.sourceIndex] = payload.retainedBefore;
    graph.sources[payload.beforeSourceCount] = payload.beforeSourceTail;
    graph.sourceCount = payload.beforeSourceCount;
    graph.nextSourceId = payload.beforeNextSourceId;
    if (payload.triggerCreated) {
        graph.triggerBindings[payload.beforeTriggerCount] =
            payload.beforeTriggerTail;
        graph.triggerBindingCount = payload.beforeTriggerCount;
    }
    graph.nextBindingId = payload.beforeNextBindingId;
    if (payload.sharedCurveReferenceCreated) {
        auto* record = const_cast<core::state::modulation::ProjectCurveRecord*>(
            core::state::modulation::findProjectCurve(
                pages.control.authored.curves,
                payload.sharedCurveId
            )
        );
        if (!record || record->referenceCount == 0U) return false;
        --record->referenceCount;
    }
    pages.control.markAuthoredMutation();
    return true;
}

FLASHMEM bool restoreSplitAfter(
    MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
) {
    if (!splitBeforeMatches(pages, payload)) return false;
    auto& graph = pages.control.authored.modulation;
    if (payload.sharedCurveReferenceCreated) {
        auto* record = const_cast<core::state::modulation::ProjectCurveRecord*>(
            core::state::modulation::findProjectCurve(
                pages.control.authored.curves,
                payload.sharedCurveId
            )
        );
        if (!record) return false;
        ++record->referenceCount;
    }
    graph.sources[payload.sourceIndex] = payload.retainedAfter;
    graph.sources[payload.beforeSourceCount] = payload.clone;
    graph.sourceCount = static_cast<uint16_t>(payload.beforeSourceCount + 1U);
    graph.nextSourceId = payload.afterNextSourceId;
    for (uint16_t index = 0; index < payload.movedBindingCount; ++index) {
        const auto& entry = payload.movedBindings[index];
        graph.outputBindings[entry.globalIndex] = entry.after;
    }
    if (payload.triggerCreated) {
        graph.triggerBindings[payload.beforeTriggerCount] = payload.cloneTrigger;
        graph.triggerBindingCount = static_cast<uint16_t>(
            payload.beforeTriggerCount + 1U
        );
    }
    graph.nextBindingId = payload.afterNextBindingId;
    pages.control.markAuthoredMutation();
    return true;
}

FLASHMEM uint32_t auditionGeneration(
    uint32_t revision,
    core::state::modulation::ModulatorId sourceId,
    core::state::modulation::ModulationBindingId bindingId
) {
    uint32_t generation = revision ^ (sourceId.value * 0x9E3779B9UL) ^
                          (bindingId.value * 0x85EBCA6BUL);
    return generation == 0U ? 1U : generation;
}

struct MacroModulationBindingWorkBuffer {
    std::array<
        core::state::modulation::ModulationBindingState,
        core::state::modulation::PROJECT_MODULATION_BINDING_CAPACITY
    > bindings;
};

FLASHMEM uint32_t hashBinding(uint32_t hash,
                              const core::state::modulation::ModulationBindingState& binding) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&binding);
    for (size_t index = 0; index < sizeof(binding); ++index) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }
    return hash;
}

FLASHMEM uint32_t unrelatedBindingHash(
    const core::state::modulation::ProjectModulationState& graph,
    const core::state::modulation::ModulationDestination& destination
) {
    uint32_t hash = 2166136261UL;
    uint16_t count = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination == destination) continue;
        hash = hashBinding(hash, binding);
        ++count;
    }
    hash ^= count;
    hash *= 16777619UL;
    return hash;
}

FLASHMEM bool captureModulationAssignments(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroModulationAssignmentSnapshot& out
) {
    if (!macroAutomationAddressValid(address)) return false;
    out = {};
    const auto destination =
        core::state::modulation::projectControlDestination(address);
    const auto& graph = pages.control.authored.modulation;
    out.destination = destination;
    out.nextBindingId = graph.nextBindingId;
    out.globalBindingCount = graph.outputBindingCount;
    out.unrelatedHash = unrelatedBindingHash(graph, destination);
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (out.assignmentCount >= out.assignments.size()) return false;
        out.assignments[out.assignmentCount++] = {
            .binding = binding,
            .globalIndex = index,
        };
    }
    return true;
}

FLASHMEM bool sameModulationAssignments(
    const MacroModulationAssignmentSnapshot& lhs,
    const MacroModulationAssignmentSnapshot& rhs
) {
    if (lhs.destination != rhs.destination ||
        lhs.nextBindingId != rhs.nextBindingId ||
        lhs.unrelatedHash != rhs.unrelatedHash ||
        lhs.globalBindingCount != rhs.globalBindingCount ||
        lhs.assignmentCount != rhs.assignmentCount) {
        return false;
    }
    for (uint16_t index = 0; index < lhs.assignmentCount; ++index) {
        if (lhs.assignments[index].globalIndex !=
                rhs.assignments[index].globalIndex ||
            !sameObjectBits(
                lhs.assignments[index].binding,
                rhs.assignments[index].binding
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool liveModulationAssignmentsMatch(
    const MacroPagesState& pages,
    const MacroModulationAssignmentSnapshot& expected
) {
    const auto& graph = pages.control.authored.modulation;
    if (graph.outputBindingCount != expected.globalBindingCount ||
        graph.nextBindingId != expected.nextBindingId ||
        unrelatedBindingHash(graph, expected.destination) !=
            expected.unrelatedHash) {
        return false;
    }
    uint16_t assignment = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != expected.destination) continue;
        if (assignment >= expected.assignmentCount ||
            expected.assignments[assignment].globalIndex != index ||
            !sameObjectBits(
                expected.assignments[assignment].binding,
                binding
            )) {
            return false;
        }
        ++assignment;
    }
    return assignment == expected.assignmentCount;
}

FLASHMEM bool applyModulationAssignments(
    MacroPagesState& pages,
    const MacroModulationAssignmentSnapshot& target
) {
    using namespace core::state::modulation;
    auto& graph = pages.control.authored.modulation;
    if (target.assignmentCount > target.assignments.size() ||
        target.globalBindingCount > graph.outputBindings.size() ||
        unrelatedBindingHash(graph, target.destination) !=
            target.unrelatedHash) {
        return false;
    }

    uint16_t currentAssignmentCount = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].destination == target.destination) {
            ++currentAssignmentCount;
        }
    }
    const uint16_t unrelatedCount = static_cast<uint16_t>(
        graph.outputBindingCount - currentAssignmentCount
    );
    if (static_cast<uint32_t>(unrelatedCount) + target.assignmentCount !=
        target.globalBindingCount) {
        return false;
    }

    uint16_t previousIndex = 0;
    for (uint16_t index = 0; index < target.assignmentCount; ++index) {
        const auto& entry = target.assignments[index];
        if (entry.binding.destination != target.destination ||
            entry.globalIndex >= target.globalBindingCount ||
            (index > 0U && entry.globalIndex <= previousIndex)) {
            return false;
        }
        previousIndex = entry.globalIndex;
    }

    auto work = core::app::makeExtmemUnique<MacroModulationBindingWorkBuffer>();
    if (!work) return false;
    uint16_t assignmentCursor = 0;
    uint16_t unrelatedCursor = 0;
    for (uint16_t outputIndex = 0;
         outputIndex < target.globalBindingCount;
         ++outputIndex) {
        if (assignmentCursor < target.assignmentCount &&
            target.assignments[assignmentCursor].globalIndex == outputIndex) {
            work->bindings[outputIndex] =
                target.assignments[assignmentCursor++].binding;
            continue;
        }
        while (unrelatedCursor < graph.outputBindingCount &&
               graph.outputBindings[unrelatedCursor].destination ==
                   target.destination) {
            ++unrelatedCursor;
        }
        if (unrelatedCursor >= graph.outputBindingCount) return false;
        work->bindings[outputIndex] = graph.outputBindings[unrelatedCursor++];
    }
    while (unrelatedCursor < graph.outputBindingCount &&
           graph.outputBindings[unrelatedCursor].destination ==
               target.destination) {
        ++unrelatedCursor;
    }
    if (assignmentCursor != target.assignmentCount ||
        unrelatedCursor != graph.outputBindingCount) {
        return false;
    }

    const uint16_t previousCount = graph.outputBindingCount;
    for (uint16_t index = 0; index < target.globalBindingCount; ++index) {
        graph.outputBindings[index] = work->bindings[index];
    }
    for (uint16_t index = target.globalBindingCount;
         index < previousCount;
         ++index) {
        graph.outputBindings[index] = {};
    }
    graph.outputBindingCount = target.globalBindingCount;
    graph.nextBindingId = target.nextBindingId;
    pages.control.markAuthoredMutation();
    return true;
}

}  // namespace

FLASHMEM bool captureMacroSlotHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotHistorySnapshot& out
) {
    if (!macroAutomationAddressValid(address)) return false;
    out = {};
    out.address = address;
    const auto& page = pages.pageData(address.track, address.page);
    out.macroActive = page.isMacroActive(address.macro);
    out.cc = page.cc[address.macro];
    out.staticValue = page.values[address.macro];

    if (!core::state::modulation::captureProjectControlMacroSlot(
            pages.control,
            address,
            out.slot,
            out.points.data(),
            static_cast<uint16_t>(out.points.size()),
            out.automationPointCount,
            out.modulationPointCount
        )) {
        return false;
    }
    out.slotPresent = macroCurveStored(out.slot.automation) ||
                      macroCurveStored(out.slot.modulation);
    normalizeCurveOffsets(out);
    return snapshotConsistent(out);
}

FLASHMEM bool sameMacroSlotHistorySnapshot(
    const MacroSlotHistorySnapshot& lhs,
    const MacroSlotHistorySnapshot& rhs
) {
    if (!sameAddress(lhs.address, rhs.address) ||
        lhs.macroActive != rhs.macroActive ||
        lhs.cc != rhs.cc ||
        !sameFloatBits(lhs.staticValue, rhs.staticValue) ||
        lhs.slotPresent != rhs.slotPresent ||
        lhs.automationPointCount != rhs.automationPointCount ||
        lhs.modulationPointCount != rhs.modulationPointCount) {
        return false;
    }
    if (!lhs.slotPresent) return true;
    if (!sameCurveMetadata(lhs.slot.automation, rhs.slot.automation) ||
        !sameCurveMetadata(lhs.slot.modulation, rhs.slot.modulation) ||
        !sameFloatBits(lhs.slot.modulationDepth, rhs.slot.modulationDepth)) {
        return false;
    }
    const uint16_t count = snapshotPointCount(lhs);
    for (uint16_t i = 0; i < count; ++i) {
        if (!samePoint(lhs.points[i], rhs.points[i])) return false;
    }
    return true;
}

FLASHMEM bool liveMacroSlotMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
) {
    if (!snapshotConsistent(snapshot)) return false;
    const auto& address = snapshot.address;
    const auto& page = pages.pageData(address.track, address.page);
    if (page.isMacroActive(address.macro) != snapshot.macroActive ||
        page.cc[address.macro] != snapshot.cc ||
        !sameFloatBits(page.values[address.macro], snapshot.staticValue)) {
        return false;
    }

    core::state::modulation::ProjectControlMacroSlotView live{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            live
        ) || live.present != snapshot.slotPresent) {
        return false;
    }
    if (!live.present) return true;
    return sameFloatBits(
               live.legacy.modulationDepth,
               snapshot.slot.modulationDepth
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.automationCurveId,
               live.legacy.automation,
               snapshot.slot.automation,
               snapshot,
               0
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.modulationCurveId,
               live.legacy.modulation,
               snapshot.slot.modulation,
               snapshot,
               snapshot.automationPointCount
           );
}

FLASHMEM bool applyMacroSlotHistorySnapshot(
    MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
) {
    if (!snapshotConsistent(snapshot)) return false;
    const auto& address = snapshot.address;
    const uint16_t required = snapshotPointCount(snapshot);
    MacroAutomationSlotState empty{};
    const auto& slot = snapshot.slotPresent ? snapshot.slot : empty;
    const auto* points = required > 0U ? snapshot.points.data() : nullptr;
    if (!core::state::modulation::replaceProjectControlMacroSlot(
            pages.control,
            address,
            slot,
            points,
            required
        )) {
        return false;
    }

    auto& page = pages.pageData(address.track, address.page);
    page.setMacroActive(address.macro, snapshot.macroActive);
    page.cc[address.macro] = snapshot.cc;
    page.values[address.macro] = snapshot.staticValue;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool captureMacroAutomationHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroAutomationHistorySnapshot& out
) {
    if (!macroAutomationAddressValid(address)) return false;
    out = MacroAutomationHistorySnapshot{};
    out.address = address;

    core::state::modulation::ProjectControlMacroSlotView view{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            view
        )) {
        return false;
    }
    out.automation = view.legacy.automation;
    out.automation.pointOffset = 0U;
    if (!view.automationStored) {
        return automationSnapshotConsistent(out);
    }

    const auto* record = core::state::modulation::findProjectCurve(
        pages.control.authored.curves,
        view.automationCurveId
    );
    if (record == nullptr ||
        record->pointCount != view.legacy.automation.pointCount ||
        record->pointCount > MACRO_AUTOMATION_RECORDING_MAX_POINTS ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            pages.control.authored.curves.pointCount) {
        return false;
    }

    out.pointCount = record->pointCount;
    out.points = core::app::makeExtmemUniqueArrayForOverwrite<
        MacroPackedCurvePoint
    >(out.pointCount);
    if (!out.points) return false;
    for (uint16_t index = 0; index < out.pointCount; ++index) {
        const auto& point = pages.control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + index)
        ];
        out.points[index] = {.tick = point.tick, .value = point.value};
    }
    return automationSnapshotConsistent(out);
}

FLASHMEM bool sameMacroAutomationHistorySnapshot(
    const MacroAutomationHistorySnapshot& lhs,
    const MacroAutomationHistorySnapshot& rhs
) {
    if (!automationSnapshotConsistent(lhs) ||
        !automationSnapshotConsistent(rhs) ||
        !sameAddress(lhs.address, rhs.address) ||
        lhs.pointCount != rhs.pointCount ||
        !sameCurveMetadata(lhs.automation, rhs.automation)) {
        return false;
    }
    for (uint16_t index = 0; index < lhs.pointCount; ++index) {
        if (!samePoint(lhs.points[index], rhs.points[index])) return false;
    }
    return true;
}

FLASHMEM bool liveMacroAutomationMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
) {
    if (!automationSnapshotConsistent(snapshot)) return false;
    core::state::modulation::ProjectControlMacroSlotView live{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            snapshot.address,
            live
        ) ||
        live.automationStored != (snapshot.pointCount > 0U) ||
        !sameCurveMetadata(live.legacy.automation, snapshot.automation)) {
        return false;
    }
    if (!live.automationStored) return true;

    const auto* record = core::state::modulation::findProjectCurve(
        pages.control.authored.curves,
        live.automationCurveId
    );
    if (record == nullptr || record->pointCount != snapshot.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            pages.control.authored.curves.pointCount) {
        return false;
    }
    for (uint16_t index = 0; index < snapshot.pointCount; ++index) {
        const auto& point = pages.control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + index)
        ];
        if (!samePoint(
                MacroPackedCurvePoint{point.tick, point.value},
                snapshot.points[index]
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool applyMacroAutomationHistorySnapshot(
    MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
) {
    if (!automationSnapshotConsistent(snapshot)) return false;
    return core::state::modulation::replaceProjectControlAutomation(
        pages.control,
        snapshot.address,
        snapshot.automation,
        snapshot.pointCount > 0U ? snapshot.points.get() : nullptr,
        snapshot.pointCount
    );
}

FLASHMEM MacroHistoryService::MacroHistoryService() = default;
FLASHMEM MacroHistoryService::~MacroHistoryService() = default;

FLASHMEM MacroHistoryChangePtr MacroHistoryService::prepare(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroHistoryActionKind kind
) const {
    if (pendingModulatorSlot_() != nullptr) return {};
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->slot = core::app::makeExtmemUnique<MacroSlotHistoryChangePayload>();
    if (!change->slot) return {};
    change->kind = kind;
    change->address = address;
    if (!captureMacroSlotHistorySnapshot(
            pages,
            address,
            change->slot->before
        )) {
        return {};
    }
    return change;
}

FLASHMEM MacroHistoryChangePtr
MacroHistoryService::prepareAutomationRecording(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) const {
    if (pendingModulatorSlot_() != nullptr) return {};
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->automation = core::app::makeExtmemUnique<
        MacroAutomationHistoryPayload
    >();
    if (!change->automation) return {};
    change->kind = MacroHistoryActionKind::RECORD_AUTOMATION;
    change->address = address;
    if (!captureMacroAutomationHistorySnapshot(
            pages,
            address,
            change->automation->before
        )) {
        return {};
    }
    return change;
}

FLASHMEM MacroHistoryChangePtr
MacroHistoryService::prepareModulationAssignments_(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroHistoryActionKind kind
) const {
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return {};
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->modulationAssignments = core::app::makeExtmemUnique<
        MacroModulationAssignmentsHistoryPayload
    >();
    if (!change->modulationAssignments) return {};
    change->kind = kind;
    change->address = address;
    if (!captureModulationAssignments(
            pages,
            address,
            change->modulationAssignments->before
        )) {
        return {};
    }
    return change;
}

FLASHMEM bool MacroHistoryService::commitModulationAssignments_(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (!change || !change->modulationAssignments) return false;
    auto& payload = *change->modulationAssignments;
    if (!captureModulationAssignments(pages, change->address, payload.after)) {
        (void)applyModulationAssignments(pages, payload.before);
        return false;
    }
    if (sameModulationAssignments(payload.before, payload.after)) return false;

    if (coalesce && coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == change->kind &&
        sameAddress(coalesced_address_, change->address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->modulationAssignments &&
            sameModulationAssignments(
                previous->modulationAssignments->after,
                payload.before
            )) {
            previous->modulationAssignments->after = payload.after;
            clearRedo_();
            return true;
        }
    }

    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = undo_[undo_count_ - 1U]->kind;
        coalesced_address_ = undo_[undo_count_ - 1U]->address;
    }
    return true;
}

FLASHMEM bool MacroHistoryService::commitProjectSourceEdit_(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (!change || !change->sourceEdit.valid ||
        change->kind != MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT ||
        change->sourceEdit.before.id != change->sourceEdit.after.id) {
        return false;
    }
    const auto* live = core::state::modulation::findProjectModulator(
        pages.control.authored.modulation,
        change->sourceEdit.after.id
    );
    if (live == nullptr ||
        !sameObjectBits(*live, change->sourceEdit.after) ||
        sameObjectBits(
            change->sourceEdit.before,
            change->sourceEdit.after
        )) {
        return false;
    }

    if (coalesce && coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == change->kind) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->sourceEdit.valid &&
            previous->sourceEdit.after.id == change->sourceEdit.before.id &&
            sameObjectBits(
                previous->sourceEdit.after,
                change->sourceEdit.before
            )) {
            previous->sourceEdit.after = change->sourceEdit.after;
            clearRedo_();
            return true;
        }
    }

    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    }
    return true;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginLfoModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorLfoDraft& sourceDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (!macroAutomationAddressValid(address) ||
        bindingDraft.destination != projectControlDestination(address) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return failure;
    change->kind = MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT;
    change->address = address;
    auto& payload = change->modulator;
    auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY) {
        failure.status = graph.sourceCount >= PROJECT_MODULATOR_CAPACITY
            ? ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
            : ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED;
        return failure;
    }
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    payload.beforeBindingTail = graph.outputBindings[graph.outputBindingCount];
    payload.sourceCreated = true;
    payload.bindingCreated = true;
    payload.pending = true;
    MacroHistoryChange* reserved = change.get();
    if (!parkPending_(std::move(change))) return failure;

    const auto created = createLfoModulator(graph, sourceDraft);
    if (!created.changed()) {
        (void)takePending_();
        return created;
    }
    auto binding = bindingDraft;
    binding.sourceId = created.sourceId;
    const auto bound = addProjectModulationBinding(graph, binding);
    if (!bound.changed()) {
        restoreCreationBefore(pages.control, payload, true);
        (void)takePending_();
        return bound;
    }

    payload.source = graph.sources[payload.beforeSourceCount];
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    payload.generation = auditionGeneration(
        pages.control.authoredRevision,
        created.sourceId,
        bound.bindingId
    );
    pages.control.audition = {
        .sourceId = created.sourceId,
        .bindingId = bound.bindingId,
        .destination = binding.destination,
        .generation = payload.generation,
        .active = true,
        .sourceCreated = true,
    };
    (void)reserved;
    return {
        .status = ProjectModulationStatus::OK,
        .sourceId = created.sourceId,
        .bindingId = bound.bindingId,
    };
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createUnassignedLfo(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorLfoDraft& sourceDraft
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (sourceDraft.reach.kind != ModulatorReachKind::DETACHED ||
        !validModulatorReach(sourceDraft.reach) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }
    auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        failure.status = ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED;
        return failure;
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return failure;
    change->kind = MacroHistoryActionKind::CREATE_PROJECT_MODULATOR;
    auto& payload = change->modulator;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    payload.sourceCreated = true;
    payload.bindingCreated = false;

    const auto created = createLfoModulator(graph, sourceDraft);
    if (!created.changed()) return created;
    payload.source = graph.sources[payload.beforeSourceCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    endCoalescing();
    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    return created;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::duplicateProjectModulator(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const char* cloneName
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (cloneName == nullptr || cloneName[0] == '\0' ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }
    auto& graph = pages.control.authored.modulation;
    auto& arena = pages.control.authored.curves;
    const auto* source = findProjectModulator(graph, sourceId);
    if (!source) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        failure.status = ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED;
        return failure;
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::CREATE_PROJECT_MODULATOR;
    auto& payload = change->modulator;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    payload.sourceCreated = true;
    payload.bindingCreated = false;
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

    const auto duplicated = core::state::modulation::duplicateProjectModulator(
        graph,
        arena,
        sourceId,
        cloneName
    );
    if (!duplicated.changed()) return duplicated;
    payload.source = graph.sources[payload.beforeSourceCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    endCoalescing();
    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    return duplicated;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginExistingModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    const core::state::modulation::ModulatorReach* widenedReach
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (!macroAutomationAddressValid(address) || !valid(sourceId) ||
        bindingDraft.destination != projectControlDestination(address) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }

    auto& graph = pages.control.authored.modulation;
    const auto* source = findProjectModulator(graph, sourceId);
    if (source == nullptr) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }
    const ModulatorReach effectiveReach = widenedReach ? *widenedReach
                                                       : source->reach;
    if (!validModulatorReach(effectiveReach) ||
        !modulatorReachContains(effectiveReach, bindingDraft.destination)) {
        failure.status = ProjectModulationStatus::REACH_VIOLATION;
        return failure;
    }
    if (graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY) {
        failure.status = ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED;
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return failure;
    change->kind = MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT;
    change->address = address;
    auto& payload = change->modulator;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSource = *source;
    payload.source = *source;
    payload.beforeBindingTail = graph.outputBindings[graph.outputBindingCount];
    payload.sourceCreated = false;
    payload.bindingCreated = true;
    payload.pending = true;
    MacroHistoryChange* reserved = change.get();
    if (!parkPending_(std::move(change))) return failure;

    if (widenedReach != nullptr &&
        !core::state::modulation::setProjectModulatorReach(
             graph,
             sourceId,
             *widenedReach
         ).changed()) {
        if (source->reach.kind != widenedReach->kind ||
            std::memcmp(&source->reach, widenedReach, sizeof(*widenedReach)) != 0) {
            (void)takePending_();
            return failure;
        }
    }

    auto binding = bindingDraft;
    binding.sourceId = sourceId;
    const auto bound = addProjectModulationBinding(graph, binding);
    if (!bound.changed()) {
        if (widenedReach != nullptr) {
            auto* liveSource = findProjectModulator(graph, sourceId);
            if (liveSource != nullptr) *liveSource = payload.beforeSource;
        }
        (void)takePending_();
        return bound;
    }

    auto& committedPayload = reserved->modulator;
    committedPayload.binding =
        graph.outputBindings[committedPayload.beforeBindingCount];
    committedPayload.afterNextSourceId = graph.nextSourceId;
    committedPayload.afterNextBindingId = graph.nextBindingId;
    pages.control.markAuthoredMutation();
    committedPayload.generation = auditionGeneration(
        pages.control.authoredRevision,
        sourceId,
        bound.bindingId
    );
    pages.control.audition = {
        .sourceId = sourceId,
        .bindingId = bound.bindingId,
        .destination = binding.destination,
        .generation = committedPayload.generation,
        .active = true,
        .sourceCreated = false,
    };
    return {
        .status = ProjectModulationStatus::OK,
        .sourceId = sourceId,
        .bindingId = bound.bindingId,
    };
}

FLASHMEM bool MacroHistoryService::cancelModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    auto* slot = pendingModulatorSlot_();
    if (slot == nullptr || !*slot) return false;
    auto& change = **slot;
    auto& payload = change.modulator;
    const auto& audition = pages.control.audition;
    if (!payload.pending || !sameAddress(change.address, address) ||
        !audition.active || audition.generation != payload.generation ||
        audition.sourceId != payload.source.id ||
        audition.bindingId != payload.binding.id ||
        !creationIdentityMatches(pages.control, payload, false)) {
        return false;
    }
    restoreCreationBefore(pages.control, payload, true);
    pages.control.audition = {};
    (void)takePending_();
    return true;
}

FLASHMEM bool MacroHistoryService::commitModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    auto* slot = pendingModulatorSlot_();
    if (slot == nullptr || !*slot) return false;
    auto& change = **slot;
    auto& payload = change.modulator;
    const auto& audition = pages.control.audition;
    if (!payload.pending || !sameAddress(change.address, address) ||
        !audition.active || audition.generation != payload.generation ||
        !creationIdentityMatches(pages.control, payload, false)) {
        return false;
    }
    const auto& graph = pages.control.authored.modulation;
    const auto* source = core::state::modulation::findProjectModulator(
        graph,
        audition.sourceId
    );
    if (source == nullptr) return false;
    payload.source = *source;
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    payload.afterNextSourceId = graph.nextSourceId;
    payload.afterNextBindingId = graph.nextBindingId;
    auto committed = takePending_();
    if (!committed) return false;
    committed->modulator.pending = false;
    pages.control.audition = {};
    endCoalescing();
    push_(undo_, undo_count_, std::move(committed));
    clearRedo_();
    return true;
}

FLASHMEM bool MacroHistoryService::modulatorAuditionPending(
    const MacroAutomationSlotAddress& address
) const {
    const auto* slot = pendingModulatorSlot_();
    return slot != nullptr && *slot && (*slot)->modulator.pending &&
           sameAddress((*slot)->address, address);
}

FLASHMEM bool MacroHistoryService::commitPrepared(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (change && change->automation) {
        if (coalesce ||
            change->kind != MacroHistoryActionKind::RECORD_AUTOMATION ||
            !sameAddress(
                change->address,
                change->automation->before.address
            )) {
            return false;
        }
        auto& payload = *change->automation;
        if (!captureMacroAutomationHistorySnapshot(
                pages,
                change->address,
                payload.after
            )) {
            (void)applyMacroAutomationHistorySnapshot(pages, payload.before);
            return false;
        }
        if (sameMacroAutomationHistorySnapshot(
                payload.before,
                payload.after
            )) {
            return false;
        }
        push_(undo_, undo_count_, std::move(change));
        clearRedo_();
        endCoalescing();
        return true;
    }
    if (!change || !change->slot ||
        !sameAddress(change->address, change->slot->before.address)) {
        return false;
    }
    if (!captureMacroSlotHistorySnapshot(
            pages,
            change->address,
            change->slot->after
        )) {
        (void)applyMacroSlotHistorySnapshot(pages, change->slot->before);
        return false;
    }
    if (sameMacroSlotHistorySnapshot(
            change->slot->before,
            change->slot->after
        )) {
        return false;
    }

    if (coalesce && coalescing_ && undo_count_ > 0 &&
        coalesced_kind_ == change->kind &&
        sameAddress(coalesced_address_, change->address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->slot &&
            sameMacroSlotHistorySnapshot(
                previous->slot->after,
                change->slot->before
            )) {
            previous->slot->after = change->slot->after;
            clearRedo_();
            return true;
        }
    }

    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = undo_[undo_count_ - 1U]->kind;
        coalesced_address_ = undo_[undo_count_ - 1U]->address;
    }
    return true;
}

FLASHMEM bool MacroHistoryService::setModulationDepthCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    float depth
) {
    const auto bindingId =
        core::state::modulation::projectControlFocusedModulationBinding(
            pages.control,
            address
        );
    return setModulationBindingDepthCoalesced(
        pages,
        address,
        bindingId,
        depth
    );
}

FLASHMEM bool MacroHistoryService::setModulationBindingDepthCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId,
    float depth
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address) || !valid(bindingId) ||
        !std::isfinite(depth)) {
        return false;
    }
    auto* binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    const auto destination = projectControlDestination(address);
    if (binding == nullptr || binding->destination != destination) return false;
    const long rounded = std::lround(
        std::clamp(depth, -1.0f, 1.0f) * 32767.0f
    );
    const int16_t amountQ15 = static_cast<int16_t>(
        std::clamp<long>(rounded, -32767L, 32767L)
    );
    if (binding->amountQ15 == amountQ15) return false;

    if (coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == MacroHistoryActionKind::DEPTH_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->modulationAssignments &&
            liveModulationAssignmentsMatch(
                pages,
                previous->modulationAssignments->after
            )) {
            const auto beforeBinding = *binding;
            const bool enabled =
                (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
            if (!updateProjectModulationBinding(
                    pages.control.authored.modulation,
                    bindingId,
                    amountQ15,
                    binding->application,
                    binding->transfer,
                    enabled,
                    binding->slewMs
                ).changed()) {
                return false;
            }
            pages.control.markAuthoredMutation();
            if (!captureModulationAssignments(
                    pages,
                    address,
                    previous->modulationAssignments->after
                )) {
                *binding = beforeBinding;
                pages.control.markAuthoredMutation();
                return false;
            }
            clearRedo_();
            return true;
        }
    }

    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::DEPTH_EDIT
    );
    if (!change) return false;
    binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    if (binding == nullptr) return false;
    const bool enabled =
        (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
    if (!updateProjectModulationBinding(
            pages.control.authored.modulation,
            bindingId,
            amountQ15,
            binding->application,
            binding->transfer,
            enabled,
            binding->slewMs
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change), true);
}

FLASHMEM bool MacroHistoryService::setModulationBindingEnabled(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId,
    bool enabled
) {
    using namespace core::state::modulation;
    auto* binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    if (binding == nullptr ||
        binding->destination != projectControlDestination(address)) {
        return false;
    }
    const bool current =
        (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
    if (current == enabled) return false;
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::SOURCE_STATE
    );
    if (!change) return false;
    if (!updateProjectModulationBinding(
            pages.control.authored.modulation,
            bindingId,
            binding->amountQ15,
            binding->application,
            binding->transfer,
            enabled,
            binding->slewMs
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

FLASHMEM bool MacroHistoryService::setAllModulationBindingsEnabled(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    bool enabled
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address)) return false;
    const auto destination = projectControlDestination(address);
    bool needsChange = false;
    const auto& graph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination == destination &&
            ((binding.flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U) !=
                enabled) {
            needsChange = true;
            break;
        }
    }
    if (!needsChange) return false;
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::SOURCE_STATE
    );
    if (!change) return false;
    auto& mutableGraph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < mutableGraph.outputBindingCount; ++index) {
        auto& binding = mutableGraph.outputBindings[index];
        if (binding.destination != destination) continue;
        binding.flags = enabled
            ? static_cast<uint8_t>(
                  binding.flags | PROJECT_MODULATION_BINDING_FLAG_ENABLED
              )
            : static_cast<uint8_t>(
                  binding.flags & ~PROJECT_MODULATION_BINDING_FLAG_ENABLED
              );
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

FLASHMEM bool MacroHistoryService::removeModulationBinding(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId
) {
    using namespace core::state::modulation;
    const auto* binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    if (binding == nullptr ||
        binding->destination != projectControlDestination(address)) {
        return false;
    }
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::REMOVE_MODULATOR_ASSIGNMENT
    );
    if (!change ||
        !removeProjectModulationBinding(
            pages.control.authored.modulation,
            bindingId
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

FLASHMEM bool MacroHistoryService::clearModulationBindings(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address)) return false;
    const auto destination = projectControlDestination(address);
    auto& graph = pages.control.authored.modulation;
    bool stored = false;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].destination == destination) {
            stored = true;
            break;
        }
    }
    if (!stored) return false;
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::CLEAR_MODULATION
    );
    if (!change) return false;
    for (uint16_t cursor = graph.outputBindingCount; cursor > 0U; --cursor) {
        const auto binding = graph.outputBindings[cursor - 1U];
        if (binding.destination == destination &&
            !removeProjectModulationBinding(graph, binding.id).changed()) {
            (void)applyModulationAssignments(
                pages,
                change->modulationAssignments->before
            );
            return false;
        }
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

FLASHMEM bool MacroHistoryService::pasteModulationBinding(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulationBindingDraft& draft,
    bool overwriteExisting,
    core::state::modulation::ModulationBindingId* appliedBinding
) {
    using namespace core::state::modulation;
    const auto destination = projectControlDestination(address);
    if (!macroAutomationAddressValid(address) ||
        draft.destination != destination || !valid(draft.sourceId)) {
        return false;
    }

    auto& graph = pages.control.authored.modulation;
    ModulationBindingState* existing = nullptr;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        auto& candidate = graph.outputBindings[index];
        if (candidate.sourceId == draft.sourceId &&
            candidate.destination == destination) {
            existing = &candidate;
            break;
        }
    }
    if ((existing != nullptr) != overwriteExisting) return false;

    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::PASTE_MODULATION
    );
    if (!change) return false;

    ProjectModulationResult mutation{};
    if (existing != nullptr) {
        mutation = updateProjectModulationBinding(
            graph,
            existing->id,
            draft.amountQ15,
            draft.application,
            draft.transfer,
            draft.enabled,
            draft.slewMs
        );
    } else {
        mutation = addProjectModulationBinding(graph, draft);
    }
    if (!mutation.changed()) return false;
    pages.control.markAuthoredMutation();
    if (!commitModulationAssignments_(pages, std::move(change))) return false;
    if (appliedBinding != nullptr) *appliedBinding = mutation.bindingId;
    return true;
}

FLASHMEM bool MacroHistoryService::setProjectModulatorEnabled(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    bool enabled
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectModulatorEnabled(
        pages.control.authored.modulation,
        sourceId,
        enabled
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->sourceEdit.after = *findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    change->sourceEdit.valid = true;
    endCoalescing();
    return commitProjectSourceEdit_(pages, std::move(change), false);
}

FLASHMEM bool MacroHistoryService::setProjectLfoParametersCoalesced(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulatorLfoParameters& parameters
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source || source->kind != ModulatorKind::LFO) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectLfoParameters(
        pages.control.authored.modulation,
        sourceId,
        parameters
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->sourceEdit.after = *findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    change->sourceEdit.valid = true;
    return commitProjectSourceEdit_(pages, std::move(change), true);
}

FLASHMEM bool MacroHistoryService::setProjectModulatorReach(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulatorReach& reach
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectModulatorReach(
        pages.control.authored.modulation,
        sourceId,
        reach
    );
    if (!result.changed()) return false;
    pages.control.markAuthoredMutation();
    change->sourceEdit.after = *findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    change->sourceEdit.valid = true;
    endCoalescing();
    return commitProjectSourceEdit_(pages, std::move(change), false);
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::splitProjectModulatorTrack(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    uint8_t track,
    const char* cloneName,
    const core::state::modulation::ModulatorReach& retainedReach,
    const core::state::modulation::ModulatorReach& cloneReach
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
        .retainedReach = retainedReach,
        .cloneReach = cloneReach,
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
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active ||
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
    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
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
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
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
    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    return deleted;
}

FLASHMEM void MacroHistoryService::endCoalescing() {
    coalescing_ = false;
}

FLASHMEM bool MacroHistoryService::undo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress
) {
    endCoalescing();
    if (pendingModulatorSlot_() != nullptr) return false;
    if (undo_count_ == 0) return false;
    auto& change = undo_[undo_count_ - 1U];
    if (!change) return false;
    if (change->kind == MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT ||
        change->kind == MacroHistoryActionKind::CREATE_PROJECT_MODULATOR) {
        if (!creationIdentityMatches(
                pages.control,
                change->modulator,
                true
            )) {
            return false;
        }
        restoreCreationBefore(pages.control, change->modulator, false);
    } else if (change->modulatorSplit) {
        if (!restoreSplitBefore(pages, *change->modulatorSplit)) {
            return false;
        }
    } else if (change->modulatorDelete) {
        if (!restoreDeletedModulator(pages, *change->modulatorDelete)) {
            return false;
        }
    } else if (change->sourceEdit.valid) {
        auto* source = core::state::modulation::findProjectModulator(
            pages.control.authored.modulation,
            change->sourceEdit.after.id
        );
        if (source == nullptr ||
            !sameObjectBits(*source, change->sourceEdit.after)) {
            return false;
        }
        *source = change->sourceEdit.before;
        pages.control.markAuthoredMutation();
    } else if (change->modulationAssignments) {
        if (!liveModulationAssignmentsMatch(
                pages,
                change->modulationAssignments->after
            ) ||
            !applyModulationAssignments(
                pages,
                change->modulationAssignments->before
            )) {
            return false;
        }
    } else if (change->automation) {
        if (!liveMacroAutomationMatchesHistorySnapshot(
                pages,
                change->automation->after
            ) ||
            !applyMacroAutomationHistorySnapshot(
                pages,
                change->automation->before
            )) {
            return false;
        }
    } else {
        if (!change->slot ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->after
            ) ||
            !applyMacroSlotHistorySnapshot(pages, change->slot->before)) {
            return false;
        }
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --undo_count_;
    push_(redo_, redo_count_, std::move(applied));
    return true;
}

FLASHMEM bool MacroHistoryService::redo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress
) {
    endCoalescing();
    if (pendingModulatorSlot_() != nullptr) return false;
    if (redo_count_ == 0) return false;
    auto& change = redo_[redo_count_ - 1U];
    if (!change) return false;
    if (change->kind == MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT ||
        change->kind == MacroHistoryActionKind::CREATE_PROJECT_MODULATOR) {
        if (!creationBeforeMatches(pages.control, change->modulator)) {
            return false;
        }
        restoreCreationAfter(pages.control, change->modulator);
    } else if (change->modulatorSplit) {
        if (!restoreSplitAfter(pages, *change->modulatorSplit)) {
            return false;
        }
    } else if (change->modulatorDelete) {
        if (!deleteBeforeMatches(pages, *change->modulatorDelete) ||
            !core::state::modulation::deleteProjectModulator(
                 pages.control.authored.modulation,
                 pages.control.authored.curves,
                 change->modulatorDelete->source.id
             ).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
    } else if (change->sourceEdit.valid) {
        auto* source = core::state::modulation::findProjectModulator(
            pages.control.authored.modulation,
            change->sourceEdit.before.id
        );
        if (source == nullptr ||
            !sameObjectBits(*source, change->sourceEdit.before)) {
            return false;
        }
        *source = change->sourceEdit.after;
        pages.control.markAuthoredMutation();
    } else if (change->modulationAssignments) {
        if (!liveModulationAssignmentsMatch(
                pages,
                change->modulationAssignments->before
            ) ||
            !applyModulationAssignments(
                pages,
                change->modulationAssignments->after
            )) {
            return false;
        }
    } else if (change->automation) {
        if (!liveMacroAutomationMatchesHistorySnapshot(
                pages,
                change->automation->before
            ) ||
            !applyMacroAutomationHistorySnapshot(
                pages,
                change->automation->after
            )) {
            return false;
        }
    } else {
        if (!change->slot ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->before
            ) ||
            !applyMacroSlotHistorySnapshot(pages, change->slot->after)) {
            return false;
        }
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --redo_count_;
    push_(undo_, undo_count_, std::move(applied));
    return true;
}

FLASHMEM void MacroHistoryService::clear() {
    for (auto& entry : undo_) entry.reset();
    for (auto& entry : redo_) entry.reset();
    undo_count_ = 0;
    redo_count_ = 0;
    endCoalescing();
}

FLASHMEM MacroHistoryChangePtr* MacroHistoryService::pendingModulatorSlot_() {
    for (uint8_t i = undo_count_; i < ENTRY_LIMIT; ++i) {
        if (undo_[i] && undo_[i]->modulator.pending) return &undo_[i];
    }
    for (uint8_t i = redo_count_; i < ENTRY_LIMIT; ++i) {
        if (redo_[i] && redo_[i]->modulator.pending) return &redo_[i];
    }
    return nullptr;
}

FLASHMEM const MacroHistoryChangePtr*
MacroHistoryService::pendingModulatorSlot_() const {
    for (uint8_t i = undo_count_; i < ENTRY_LIMIT; ++i) {
        if (undo_[i] && undo_[i]->modulator.pending) return &undo_[i];
    }
    for (uint8_t i = redo_count_; i < ENTRY_LIMIT; ++i) {
        if (redo_[i] && redo_[i]->modulator.pending) return &redo_[i];
    }
    return nullptr;
}

FLASHMEM bool MacroHistoryService::parkPending_(
    MacroHistoryChangePtr change
) {
    if (!change || pendingModulatorSlot_() != nullptr) return false;
    if (undo_count_ < ENTRY_LIMIT && !undo_[ENTRY_LIMIT - 1U]) {
        undo_[ENTRY_LIMIT - 1U] = std::move(change);
        return true;
    }
    if (redo_count_ < ENTRY_LIMIT && !redo_[ENTRY_LIMIT - 1U]) {
        redo_[ENTRY_LIMIT - 1U] = std::move(change);
        return true;
    }
    return false;
}

FLASHMEM MacroHistoryChangePtr MacroHistoryService::takePending_() {
    auto* slot = pendingModulatorSlot_();
    return slot != nullptr ? std::move(*slot) : MacroHistoryChangePtr{};
}

FLASHMEM void MacroHistoryService::push_(
    std::array<MacroHistoryChangePtr, ENTRY_LIMIT>& stack,
    uint8_t& count,
    MacroHistoryChangePtr change
) {
    if (!change) return;
    if (count >= ENTRY_LIMIT) {
        for (uint8_t i = 1; i < ENTRY_LIMIT; ++i) {
            stack[i - 1U] = std::move(stack[i]);
        }
        stack[ENTRY_LIMIT - 1U].reset();
        count = static_cast<uint8_t>(ENTRY_LIMIT - 1U);
    }
    stack[count++] = std::move(change);
}

FLASHMEM void MacroHistoryService::clearRedo_() {
    for (auto& entry : redo_) entry.reset();
    redo_count_ = 0;
}

}  // namespace core::state::macro
