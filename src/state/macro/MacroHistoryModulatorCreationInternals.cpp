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

FLASHMEM bool creationIdentityMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactAfter
) {
    const auto& control = pages.control;
    const auto& graph = control.authored.modulation;
    if (!historyDomainValid(control.authored)) return false;
    if (payload.beforeSourceCount > graph.sources.size() ||
        payload.beforeBindingCount > graph.outputBindings.size() ||
        payload.beforeTriggerCount > graph.triggerBindings.size() ||
        (payload.sourceCreated &&
         payload.beforeSourceCount >= graph.sources.size()) ||
        (payload.bindingCreated &&
         payload.beforeBindingCount >= graph.outputBindings.size()) ||
        (payload.triggerCreated &&
         payload.beforeTriggerCount >= graph.triggerBindings.size())) {
        return false;
    }
    const uint16_t expectedSourceCount = static_cast<uint16_t>(
        payload.beforeSourceCount + (payload.sourceCreated ? 1U : 0U)
    );
    const uint16_t expectedBindingCount = static_cast<uint16_t>(
        payload.beforeBindingCount + (payload.bindingCreated ? 1U : 0U)
    );
    const uint16_t expectedTriggerCount = static_cast<uint16_t>(
        payload.beforeTriggerCount + (payload.triggerCreated ? 1U : 0U)
    );
    if (graph.sourceCount != expectedSourceCount ||
        graph.outputBindingCount != expectedBindingCount ||
        graph.triggerBindingCount != expectedTriggerCount ||
        graph.nextSourceId != payload.afterNextSourceId ||
        graph.nextBindingId != payload.afterNextBindingId) {
        return false;
    }
    if (!macroCreationStateMatches(pages, address, payload, true)) return false;
    if (payload.destinationStructure != nullptr) {
        const bool after = payload.destinationStructure->applied;
        if (exactAfter && !after) return false;
        if (!destinationStructureMatches(
                pages,
                *payload.destinationStructure,
                after
            )) {
            return false;
        }
    }
    const core::state::modulation::ModulatorSourceState* source = nullptr;
    if (payload.sourceCreated) {
        if (payload.beforeSourceCount >= graph.sourceCount) return false;
        source = &graph.sources[payload.beforeSourceCount];
        if (source->id != payload.source.id) return false;
    } else {
        source = core::state::modulation::findProjectModulator(
            graph,
            payload.source.id
        );
        if (source == nullptr) return false;
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
    if (payload.triggerCreated) {
        const auto& trigger = graph.triggerBindings[payload.beforeTriggerCount];
        if (trigger.id != payload.trigger.id ||
            trigger.sourceId != source->id ||
            (exactAfter && !sameObjectBits(trigger, payload.trigger))) {
            return false;
        }
    }
    if (payload.sourceCreated) {
        for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
            if (graph.outputBindings[index].sourceId != source->id) continue;
            if (!payload.bindingCreated ||
                index != payload.beforeBindingCount) {
                return false;
            }
        }
        for (uint16_t index = 0U; index < graph.triggerBindingCount; ++index) {
            if (graph.triggerBindings[index].sourceId != source->id) continue;
            if (!payload.triggerCreated ||
                index != payload.beforeTriggerCount) {
                return false;
            }
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
    if (payload.recordedShape != nullptr &&
        !recordedCreationMatches(
            control,
            *payload.recordedShape,
            true
        )) {
        return false;
    }
    return !exactAfter || sameObjectBits(*source, payload.source);
}

FLASHMEM bool creationBeforeMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload
) {
    const auto& control = pages.control;
    const auto& graph = control.authored.modulation;
    if (!historyDomainValid(control.authored) ||
        graph.sourceCount != payload.beforeSourceCount ||
        graph.outputBindingCount != payload.beforeBindingCount ||
        graph.triggerBindingCount != payload.beforeTriggerCount ||
        graph.nextSourceId != payload.beforeNextSourceId ||
        graph.nextBindingId != payload.beforeNextBindingId ||
        (payload.bindingCreated &&
         !sameObjectBits(
             graph.outputBindings[payload.beforeBindingCount],
             payload.beforeBindingTail
         )) ||
        (payload.triggerCreated &&
         !sameObjectBits(
             graph.triggerBindings[payload.beforeTriggerCount],
             payload.beforeTriggerTail
         ))) {
        return false;
    }
    if (!macroCreationStateMatches(pages, address, payload, false)) return false;
    if (payload.destinationStructure != nullptr &&
        !destinationStructureMatches(
            pages,
            *payload.destinationStructure,
            false
        )) {
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
        if (payload.recordedShape != nullptr) {
            return recordedCreationMatches(
                control,
                *payload.recordedShape,
                false
            );
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
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactCancel
) {
    auto& control = pages.control;
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
    if (payload.recordedShape != nullptr) {
        restoreRecordedCreation(control, *payload.recordedShape, false);
    }
    if (payload.bindingCreated) {
        graph.outputBindings[payload.beforeBindingCount] =
            payload.beforeBindingTail;
        graph.outputBindingCount = payload.beforeBindingCount;
    }
    if (payload.triggerCreated) {
        graph.triggerBindings[payload.beforeTriggerCount] =
            payload.beforeTriggerTail;
        graph.triggerBindingCount = payload.beforeTriggerCount;
    }
    graph.nextBindingId = payload.beforeNextBindingId;
    restoreMacroCreationState(pages, address, payload, false);
    restoreDestinationStructure(pages, payload, false);
    if (exactCancel) {
        control.authoredRevision = payload.beforeAuthoredRevision;
    } else {
        control.markAuthoredMutation();
    }
}

FLASHMEM void restoreCreationAfter(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload
) {
    auto& control = pages.control;
    auto& graph = control.authored.modulation;
    if (payload.sourceCreated) {
        if (payload.recordedShape != nullptr) {
            restoreRecordedCreation(control, *payload.recordedShape, true);
        }
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
    if (payload.triggerCreated) {
        graph.triggerBindings[payload.beforeTriggerCount] = payload.trigger;
        graph.triggerBindingCount = static_cast<uint16_t>(
            payload.beforeTriggerCount + 1U
        );
    }
    graph.nextBindingId = payload.afterNextBindingId;
    restoreMacroCreationState(pages, address, payload, true);
    restoreDestinationStructure(pages, payload, true);
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

}  // namespace history_detail

}  // namespace core::state::macro
