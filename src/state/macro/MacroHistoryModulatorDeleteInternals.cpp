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

FLASHMEM bool deleteBeforeMatches(
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
    if (graph.sourceCount != payload.beforeSourceCount ||
        graph.outputBindingCount != payload.beforeBindingCount ||
        graph.triggerBindingCount != payload.beforeTriggerCount ||
        graph.destinationScaleCount != payload.beforeScaleCount ||
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
    for (uint16_t index = 0; index < payload.scaleCount; ++index) {
        const auto* live = findProjectModulationDestinationScale(
            graph,
            payload.scales[index].scale.destination
        );
        if (live == nullptr ||
            !sameObjectBits(*live, payload.scales[index].scale)) {
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
    for (uint16_t index = 0; index < payload.scaleCount; ++index) {
        const auto& scale = payload.scales[index].scale;
        if (!setProjectModulationDestinationScale(
                graph,
                scale.destination,
                scale.scaleQ15
            ).changed()) {
            return false;
        }
    }
    pages.control.markAuthoredMutation();
    return true;
}

FLASHMEM bool macroCreationStateMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool after
) {
    if (!payload.macroCreated) return true;
    if (!macroAutomationAddressValid(address)) return false;
    const auto& page = pages.pageData(address.track, address.page);
    const uint8_t activeMask = after ? payload.afterMacroActiveMask
                                     : payload.beforeMacroActiveMask;
    const uint8_t cc = after ? payload.afterMacroCc
                             : payload.beforeMacroCc;
    const float value = after ? payload.afterMacroValue
                              : payload.beforeMacroValue;
    return page.activeMacroMask == activeMask &&
           page.cc[address.macro] == cc &&
           sameFloatBits(page.values[address.macro], value);
}

FLASHMEM bool sameMacroTrackData(
    const MacroTrackData& lhs,
    const MacroTrackData& rhs
) {
    return std::memcmp(&lhs, &rhs, sizeof(MacroTrackData)) == 0;
}

FLASHMEM bool destinationStructureMatches(
    const MacroPagesState& pages,
    const MacroDestinationStructureHistoryPayload& payload,
    bool after
) {
    const uint8_t track = payload.plan.address.track;
    if (track >= TRACK_COUNT) return false;
    const uint16_t expectedMask = after
        ? payload.afterTrackEnabledMask
        : payload.beforeTrackEnabledMask;
    const uint8_t expectedActive = after
        ? payload.afterActiveTrack
        : payload.beforeActiveTrack;
    const auto& expectedTrack = after ? payload.afterTrack : payload.beforeTrack;
    return pages.currentTrackEnabledMask() == expectedMask &&
           pages.currentActiveTrack() == expectedActive &&
           sameMacroTrackData(pages.tracks[track], expectedTrack);
}

FLASHMEM bool prepareDestinationStructure(
    const MacroPagesState& pages,
    const MacroDestinationActivationPlan* plan,
    MacroModulatorCreationHistoryPayload& payload
) {
    if (plan == nullptr || !plan->changesTopology()) return true;
    if (!plan->valid || !macroAutomationAddressValid(plan->address)) {
        return false;
    }
    auto snapshot = core::app::makeExtmemUnique<
        MacroDestinationStructureHistoryPayload
    >();
    if (!snapshot) return false;
    snapshot->plan = *plan;
    snapshot->beforeTrackEnabledMask = pages.currentTrackEnabledMask();
    snapshot->beforeActiveTrack = pages.currentActiveTrack();
    snapshot->beforeTrack = pages.tracks[plan->address.track];
    if (!destinationStructureMatches(pages, *snapshot, false)) return false;
    payload.destinationStructure = std::move(snapshot);
    return true;
}

FLASHMEM bool applyDestinationStructure(
    MacroPagesState& pages,
    MacroModulatorCreationHistoryPayload& payload
) {
    auto* snapshot = payload.destinationStructure.get();
    if (snapshot == nullptr) return true;
    if (snapshot->applied ||
        !destinationStructureMatches(pages, *snapshot, false) ||
        !MacroWorkflow::applyDestinationActivation(pages, snapshot->plan)) {
        return false;
    }
    snapshot->afterTrackEnabledMask = pages.currentTrackEnabledMask();
    snapshot->afterActiveTrack = pages.currentActiveTrack();
    snapshot->afterTrack = pages.tracks[snapshot->plan.address.track];
    snapshot->applied = true;
    return destinationStructureMatches(pages, *snapshot, true);
}

FLASHMEM void restoreDestinationStructure(
    MacroPagesState& pages,
    const MacroModulatorCreationHistoryPayload& payload,
    bool after
) {
    const auto* snapshot = payload.destinationStructure.get();
    if (snapshot == nullptr || !snapshot->applied) return;
    const uint8_t track = snapshot->plan.address.track;
    pages.tracks[track] = after ? snapshot->afterTrack : snapshot->beforeTrack;
    pages.syncSharedTrackState(
        after ? snapshot->afterTrackEnabledMask
              : snapshot->beforeTrackEnabledMask,
        after ? snapshot->afterActiveTrack : snapshot->beforeActiveTrack
    );
}

FLASHMEM void restoreMacroCreationState(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool after
) {
    if (!payload.macroCreated || !macroAutomationAddressValid(address)) return;
    auto& page = pages.pageData(address.track, address.page);
    page.activeMacroMask = after ? payload.afterMacroActiveMask
                                 : payload.beforeMacroActiveMask;
    page.cc[address.macro] = after ? payload.afterMacroCc
                                   : payload.beforeMacroCc;
    page.values[address.macro] = after ? payload.afterMacroValue
                                       : payload.beforeMacroValue;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
}

FLASHMEM bool applyMacroCreation(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroModulatorCreationHistoryPayload& payload
) {
    if (!payload.macroCreated) return true;
    if (!macroAutomationAddressValid(address)) return false;
    const auto& before = pages.pageData(address.track, address.page);
    payload.beforeMacroActiveMask = before.activeMacroMask;
    payload.beforeMacroCc = before.cc[address.macro];
    payload.beforeMacroValue = before.values[address.macro];
    const auto plan = MacroWorkflow::planMacroSlotActivation(pages, address);
    if (!MacroWorkflow::applyMacroSlotActivation(pages, plan)) return false;
    const auto& after = pages.pageData(address.track, address.page);
    payload.afterMacroActiveMask = after.activeMacroMask;
    payload.afterMacroCc = after.cc[address.macro];
    payload.afterMacroValue = after.values[address.macro];
    return true;
}

}  // namespace history_detail

}  // namespace core::state::macro
