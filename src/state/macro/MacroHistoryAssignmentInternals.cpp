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
    uint16_t scaleCount = 0;
    for (uint16_t index = 0; index < graph.destinationScaleCount; ++index) {
        const auto& scale = graph.destinationScales[index];
        if (scale.destination == destination) continue;
        hash = hashBytes(hash, &scale, sizeof(scale));
        ++scaleCount;
    }
    hash = hashBytes(hash, &scaleCount, sizeof(scaleCount));
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
    out.destinationScaleQ15 =
        core::state::modulation::projectModulationDestinationScaleQ15(
            graph,
            destination
        );
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
        lhs.assignmentCount != rhs.assignmentCount ||
        lhs.destinationScaleQ15 != rhs.destinationScaleQ15) {
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
            expected.unrelatedHash ||
        projectModulationDestinationScaleQ15(
            graph,
            expected.destination
        ) != expected.destinationScaleQ15) {
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

FLASHMEM bool applyModulationAssignmentsToGraph(
    core::state::modulation::ProjectModulationState& graph,
    const MacroModulationAssignmentSnapshot& target
) {
    using namespace core::state::modulation;
    if (target.assignmentCount > target.assignments.size() ||
        target.globalBindingCount > graph.outputBindings.size() ||
        (target.assignmentCount == 0U &&
         target.destinationScaleQ15 !=
             PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) ||
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

    if (currentAssignmentCount > 0U &&
        projectModulationDestinationScaleQ15(graph, target.destination) !=
            PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 &&
        !setProjectModulationDestinationScale(
            graph,
            target.destination,
            PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15
        ).changed()) {
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
    if (target.assignmentCount > 0U &&
        target.destinationScaleQ15 !=
            PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 &&
        !setProjectModulationDestinationScale(
            graph,
            target.destination,
            target.destinationScaleQ15
        ).changed()) {
        return false;
    }
    return true;
}

FLASHMEM bool applyModulationAssignments(
    MacroPagesState& pages,
    const MacroModulationAssignmentSnapshot& target
) {
    if (!applyModulationAssignmentsToGraph(
            pages.control.authored.modulation,
            target
        )) {
        return false;
    }
    pages.control.markAuthoredMutation();
    return true;
}

}  // namespace history_detail

}  // namespace core::state::macro
