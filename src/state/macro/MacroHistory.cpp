#include "state/macro/MacroHistory.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"

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
               snapshot.modulationPointCount == 0 &&
               snapshot.destinationScaleQ15 ==
                   core::state::modulation::
                       PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
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
    if (snapshot.destinationScaleQ15 !=
            core::state::modulation::
                PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 &&
        snapshot.modulationPointCount == 0U) {
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

FLASHMEM bool automationTakePayloadConsistent(
    const MacroAutomationTakeHistoryPayload& payload,
    bool requireTouched
) {
    if (payload.track >= TRACK_COUNT || payload.page >= PAGE_COUNT ||
        payload.candidateMask == 0U ||
        (payload.candidateMask & 0xFF00U) != 0U ||
        (payload.touchedMask & ~payload.candidateMask) != 0U ||
        (requireTouched && payload.touchedMask == 0U)) {
        return false;
    }
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((payload.candidateMask & bit) == 0U) continue;
        const auto& before = payload.before[macro];
        const auto& after = payload.after[macro];
        const MacroAutomationSlotAddress expected{
            .track = payload.track,
            .page = payload.page,
            .macro = macro,
        };
        if (!macroAutomationAddressEquals(before.address, expected) ||
            !macroAutomationAddressEquals(after.address, expected) ||
            !automationSnapshotConsistent(before) || !after.points) {
            return false;
        }
        if ((payload.touchedMask & bit) != 0U &&
            !automationSnapshotConsistent(after)) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool liveAutomationTakeMatches(
    const MacroPagesState& pages,
    const MacroAutomationTakeHistoryPayload& payload,
    bool after
) {
    if (!automationTakePayloadConsistent(payload, true)) return false;
    const auto& snapshots = after ? payload.after : payload.before;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((payload.touchedMask & bit) == 0U) continue;
        if (!liveMacroAutomationMatchesHistorySnapshot(
                pages,
                snapshots[macro]
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool applyAutomationTakeAtomically(
    MacroPagesState& pages,
    const MacroAutomationTakeHistoryPayload& payload,
    bool after
) {
    if (!automationTakePayloadConsistent(payload, true)) return false;
    auto pending = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    if (!pending) return false;
    *pending = pages.control.authored;
    const auto& snapshots = after ? payload.after : payload.before;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((payload.touchedMask & bit) == 0U) continue;
        const auto& snapshot = snapshots[macro];
        if (!core::state::modulation::replaceProjectControlAutomationInDomain(
                *pending,
                snapshot.address,
                snapshot.automation,
                snapshot.pointCount > 0U ? snapshot.points.get() : nullptr,
                snapshot.pointCount
            )) {
            return false;
        }
    }
    if (!core::state::modulation::validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }
    pages.control.authored = *pending;
    pages.control.markAuthoredMutation();
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

FLASHMEM bool destinationScaleRemovedWithSource(
    const core::state::modulation::ProjectModulationState& graph,
    const core::state::modulation::ModulationDestination& destination,
    core::state::modulation::ModulatorId sourceId
) {
    bool found = false;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (binding.sourceId != sourceId) return false;
        found = true;
    }
    return found;
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
    uint16_t scaleCount = 0;
    for (uint16_t index = 0; index < graph.destinationScaleCount; ++index) {
        const auto& scale = graph.destinationScales[index];
        if (destinationScaleRemovedWithSource(
                graph,
                scale.destination,
                sourceId
            )) {
            continue;
        }
        hash = hashBytes(hash, &scale, sizeof(scale));
        ++scaleCount;
    }
    hash = hashBytes(hash, &scaleCount, sizeof(scaleCount));
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

FLASHMEM bool creationIdentityMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactAfter
) {
    const auto& control = pages.control;
    const auto& graph = control.authored.modulation;
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
    if (payload.triggerCreated) {
        const auto& trigger = graph.triggerBindings[payload.beforeTriggerCount];
        if (trigger.id != payload.trigger.id ||
            trigger.sourceId != source->id ||
            (exactAfter && !sameObjectBits(trigger, payload.trigger))) {
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
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload
) {
    const auto& control = pages.control;
    const auto& graph = control.authored.modulation;
    if (graph.sourceCount != payload.beforeSourceCount ||
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
    out.destinationScaleQ15 =
        core::state::modulation::projectModulationDestinationScaleQ15(
            pages.control.authored.modulation,
            core::state::modulation::projectControlDestination(address)
        );

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
        lhs.modulationPointCount != rhs.modulationPointCount ||
        lhs.destinationScaleQ15 != rhs.destinationScaleQ15) {
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
        ) || live.present != snapshot.slotPresent ||
        core::state::modulation::projectModulationDestinationScaleQ15(
            pages.control.authored.modulation,
            core::state::modulation::projectControlDestination(address)
        ) != snapshot.destinationScaleQ15) {
        return false;
    }
    if (!live.present) return true;
    return sameFloatBits(
               live.compatibility.modulationDepth,
               snapshot.slot.modulationDepth
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.automationCurveId,
               live.compatibility.automation,
               snapshot.slot.automation,
               snapshot,
               0
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.modulationCurveId,
               live.compatibility.modulation,
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
    if (snapshot.destinationScaleQ15 !=
            core::state::modulation::
                PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 &&
        !core::state::modulation::setProjectModulationDestinationScale(
            pages.control.authored.modulation,
            core::state::modulation::projectControlDestination(address),
            snapshot.destinationScaleQ15
        ).changed()) {
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
    out.automation = view.compatibility.automation;
    out.automation.pointOffset = 0U;
    if (!view.automationStored) {
        return automationSnapshotConsistent(out);
    }

    const auto* record = core::state::modulation::findProjectCurve(
        pages.control.authored.curves,
        view.automationCurveId
    );
    if (record == nullptr ||
        record->pointCount != view.compatibility.automation.pointCount ||
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
        !sameCurveMetadata(live.compatibility.automation, snapshot.automation)) {
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

namespace {

FLASHMEM bool captureMacroSlotRemovalState(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotRemovalState& out
) {
    if (!macroAutomationAddressValid(address) ||
        !captureMacroAutomationHistorySnapshot(
            pages,
            address,
            out.automation
        ) || !captureModulationAssignments(
            pages,
            address,
            out.modulation
        )) {
        return false;
    }
    const auto& page = pages.pageData(address.track, address.page);
    out.macroActive = page.isMacroActive(address.macro);
    out.cc = page.cc[address.macro];
    out.staticValue = page.values[address.macro];
    return true;
}

FLASHMEM bool liveMacroSlotRemovalStateMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroSlotRemovalState& expected
) {
    if (!macroAutomationAddressValid(address) ||
        !macroAutomationAddressEquals(expected.automation.address, address) ||
        expected.modulation.destination !=
            core::state::modulation::projectControlDestination(address)) {
        return false;
    }
    const auto& page = pages.pageData(address.track, address.page);
    return page.isMacroActive(address.macro) == expected.macroActive &&
           page.cc[address.macro] == expected.cc &&
           sameFloatBits(page.values[address.macro], expected.staticValue) &&
           liveMacroAutomationMatchesHistorySnapshot(
               pages,
               expected.automation
           ) && liveModulationAssignmentsMatch(pages, expected.modulation);
}

FLASHMEM bool applyMacroSlotRemovalState(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroSlotRemovalState& target
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address) ||
        !macroAutomationAddressEquals(target.automation.address, address) ||
        target.modulation.destination != projectControlDestination(address)) {
        return false;
    }

    // Removal and its history replay are cold structural operations. Build the
    // complete Project Control result in one PSRAM scratch object so failure can
    // never expose a half-cleared destination to the realtime runtime.
    auto pending = core::app::makeExtmemUnique<ProjectControlDomainState>();
    if (!pending) return false;
    *pending = pages.control.authored;
    if (!replaceProjectControlAutomationInDomain(
            *pending,
            address,
            target.automation.automation,
            target.automation.pointCount > 0U
                ? target.automation.points.get() : nullptr,
            target.automation.pointCount
        ) || !applyModulationAssignmentsToGraph(
            pending->modulation,
            target.modulation
        ) || !validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }

    pages.control.authored = *pending;
    pages.control.markAuthoredMutation();
    auto& page = pages.pageData(address.track, address.page);
    page.setMacroActive(address.macro, target.macroActive);
    page.cc[address.macro] = target.cc;
    page.values[address.macro] = target.staticValue;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

}  // namespace

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

FLASHMEM MacroHistoryChangePtr MacroHistoryService::prepareAutomationTake(
    const MacroPagesState& pages,
    uint8_t track,
    uint8_t page,
    uint16_t candidateMask
) const {
    candidateMask = static_cast<uint16_t>(candidateMask & 0x00FFU);
    if (pendingModulatorSlot_() != nullptr || candidateMask == 0U ||
        track >= TRACK_COUNT || page >= PAGE_COUNT) {
        return {};
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->automationTake = core::app::makeExtmemUnique<
        MacroAutomationTakeHistoryPayload
    >();
    if (!change->automationTake) return {};
    change->kind = MacroHistoryActionKind::RECORD_AUTOMATION;
    change->address = {.track = track, .page = page, .macro = 0U};
    auto& payload = *change->automationTake;
    payload.candidateMask = candidateMask;
    payload.track = track;
    payload.page = page;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((candidateMask & bit) == 0U) continue;
        const MacroAutomationSlotAddress address{
            .track = track,
            .page = page,
            .macro = macro,
        };
        if (!captureMacroAutomationHistorySnapshot(
                pages,
                address,
                payload.before[macro]
            )) {
            return {};
        }
        auto& after = payload.after[macro];
        after.address = address;
        after.points = core::app::makeExtmemUniqueArrayForOverwrite<
            MacroPackedCurvePoint
        >(MACRO_AUTOMATION_RECORDING_MAX_POINTS);
        if (!after.points) return {};
    }
    if (!automationTakePayloadConsistent(payload, false)) return {};
    return change;
}

FLASHMEM bool MacroHistoryService::commitPreparedAutomationTake(
    MacroPagesState& pages,
    MacroHistoryChangePtr& change
) {
    if (!change || change->kind != MacroHistoryActionKind::RECORD_AUTOMATION ||
        !change->automationTake ||
        !automationTakePayloadConsistent(*change->automationTake, true) ||
        !liveAutomationTakeMatches(pages, *change->automationTake, true)) {
        return false;
    }
    bool changed = false;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((change->automationTake->touchedMask & bit) == 0U) continue;
        if (!sameMacroAutomationHistorySnapshot(
                change->automationTake->before[macro],
                change->automationTake->after[macro]
            )) {
            changed = true;
            break;
        }
    }
    if (!changed) return false;
    endCoalescing();
    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    return true;
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
MacroHistoryService::beginNewModulatorAudition_(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorLfoDraft* lfoDraft,
    const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
    const core::state::modulation::ModulationTriggerDraft* triggerDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if ((lfoDraft == nullptr) == (adsrDraft == nullptr) ||
        !macroAutomationAddressValid(address) ||
        bindingDraft.destination != projectControlDestination(address) ||
        (destinationPlan != nullptr &&
         (!destinationPlan->valid || destinationPlan->address.track != address.track ||
          destinationPlan->address.page != address.page ||
          destinationPlan->address.macro != address.macro)) ||
        (createMacroSlot && destinationPlan != nullptr) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT;
    change->address = address;
    auto& payload = change->modulator;
    auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY ||
        (triggerDraft != nullptr &&
         graph.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY)) {
        failure.status = graph.sourceCount >= PROJECT_MODULATOR_CAPACITY
            ? ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
            : graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY
                ? ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED
                : ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED;
        return failure;
    }
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    payload.beforeBindingTail = graph.outputBindings[graph.outputBindingCount];
    if (triggerDraft != nullptr) {
        payload.beforeTriggerTail =
            graph.triggerBindings[graph.triggerBindingCount];
    }
    payload.sourceCreated = true;
    payload.bindingCreated = true;
    payload.triggerCreated = triggerDraft != nullptr;
    payload.macroCreated = createMacroSlot;
    payload.pending = true;
    if (!prepareDestinationStructure(pages, destinationPlan, payload)) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    MacroHistoryChange* reserved = change.get();
    if (!parkPending_(std::move(change))) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }

    if (!applyMacroCreation(pages, address, payload)) {
        (void)takePending_();
        return failure;
    }

    const auto created = lfoDraft != nullptr
        ? createLfoModulator(graph, *lfoDraft)
        : createAdsrModulator(graph, *adsrDraft);
    if (!created.changed()) {
        restoreMacroCreationState(pages, address, payload, false);
        (void)takePending_();
        return created;
    }
    if (triggerDraft != nullptr) {
        auto trigger = *triggerDraft;
        trigger.sourceId = created.sourceId;
        const auto triggered = addProjectModulationTrigger(graph, trigger);
        if (!triggered.changed()) {
            restoreCreationBefore(pages, address, payload, true);
            (void)takePending_();
            return triggered;
        }
    }
    auto binding = bindingDraft;
    binding.sourceId = created.sourceId;
    const auto bound = addProjectModulationBinding(graph, binding);
    if (!bound.changed()) {
        restoreCreationBefore(pages, address, payload, true);
        (void)takePending_();
        return bound;
    }

    payload.source = graph.sources[payload.beforeSourceCount];
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    if (payload.triggerCreated) {
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
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
MacroHistoryService::beginLfoModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorLfoDraft& sourceDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    return beginNewModulatorAudition_(
        pages,
        address,
        &sourceDraft,
        nullptr,
        nullptr,
        bindingDraft,
        createMacroSlot,
        destinationPlan
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::beginAdsrModulatorAudition(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
    const core::state::modulation::ModulationTriggerDraft& triggerDraft,
    const core::state::modulation::ModulationBindingDraft& bindingDraft,
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    return beginNewModulatorAudition_(
        pages,
        address,
        nullptr,
        &sourceDraft,
        &triggerDraft,
        bindingDraft,
        createMacroSlot,
        destinationPlan
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createUnassignedModulator_(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorLfoDraft* lfoDraft,
    const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
    const core::state::modulation::ModulationTriggerDraft* triggerDraft
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if ((lfoDraft == nullptr) == (adsrDraft == nullptr)) return failure;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }
    auto& graph = pages.control.authored.modulation;
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        (triggerDraft != nullptr &&
         graph.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY)) {
        failure.status = graph.sourceCount >= PROJECT_MODULATOR_CAPACITY
            ? ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
            : ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED;
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
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    if (triggerDraft != nullptr) {
        payload.beforeTriggerTail =
            graph.triggerBindings[graph.triggerBindingCount];
    }
    payload.sourceCreated = true;
    payload.bindingCreated = false;
    payload.triggerCreated = triggerDraft != nullptr;

    const auto created = lfoDraft != nullptr
        ? createLfoModulator(graph, *lfoDraft)
        : createAdsrModulator(graph, *adsrDraft);
    if (!created.changed()) return created;
    if (triggerDraft != nullptr) {
        auto trigger = *triggerDraft;
        trigger.sourceId = created.sourceId;
        const auto triggered = addProjectModulationTrigger(graph, trigger);
        if (!triggered.changed()) {
            restoreCreationBefore(pages, {}, payload, true);
            return triggered;
        }
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
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
MacroHistoryService::createUnassignedLfo(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorLfoDraft& sourceDraft
) {
    return createUnassignedModulator_(
        pages,
        &sourceDraft,
        nullptr,
        nullptr
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroHistoryService::createUnassignedAdsr(
    MacroPagesState& pages,
    const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
    const core::state::modulation::ModulationTriggerDraft& triggerDraft
) {
    return createUnassignedModulator_(
        pages,
        nullptr,
        &sourceDraft,
        &triggerDraft
    );
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
    const auto* sourceTrigger = findProjectModulationTriggerForSource(
        graph,
        sourceId
    );
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY ||
        (sourceTrigger != nullptr &&
         graph.triggerBindingCount >= PROJECT_MODULATION_TRIGGER_CAPACITY)) {
        failure.status = graph.sourceCount >= PROJECT_MODULATOR_CAPACITY
            ? ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
            : ProjectModulationStatus::TRIGGER_CAPACITY_EXCEEDED;
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
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSourceTail = graph.sources[graph.sourceCount];
    if (sourceTrigger != nullptr) {
        payload.beforeTriggerTail =
            graph.triggerBindings[graph.triggerBindingCount];
    }
    payload.sourceCreated = true;
    payload.bindingCreated = false;
    payload.triggerCreated = sourceTrigger != nullptr;
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
    if (payload.triggerCreated) {
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
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
    bool createMacroSlot,
    const MacroDestinationActivationPlan* destinationPlan
) {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (!macroAutomationAddressValid(address) || !valid(sourceId) ||
        bindingDraft.destination != projectControlDestination(address) ||
        (destinationPlan != nullptr &&
         (!destinationPlan->valid || destinationPlan->address.track != address.track ||
          destinationPlan->address.page != address.page ||
          destinationPlan->address.macro != address.macro)) ||
        (createMacroSlot && destinationPlan != nullptr) ||
        pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return failure;
    }

    auto& graph = pages.control.authored.modulation;
    const auto* source = findProjectModulator(graph, sourceId);
    if (source == nullptr) {
        failure.status = ProjectModulationStatus::INVALID_ID;
        return failure;
    }
    if (graph.outputBindingCount >= PROJECT_MODULATION_BINDING_CAPACITY) {
        failure.status = ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED;
        return failure;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    change->kind = MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT;
    change->address = address;
    auto& payload = change->modulator;
    payload.beforeSourceCount = graph.sourceCount;
    payload.beforeBindingCount = graph.outputBindingCount;
    payload.beforeTriggerCount = graph.triggerBindingCount;
    payload.beforeNextSourceId = graph.nextSourceId;
    payload.beforeNextBindingId = graph.nextBindingId;
    payload.beforeAuthoredRevision = pages.control.authoredRevision;
    payload.beforeSource = *source;
    payload.source = *source;
    payload.beforeBindingTail = graph.outputBindings[graph.outputBindingCount];
    payload.sourceCreated = false;
    payload.bindingCreated = true;
    payload.macroCreated = createMacroSlot;
    payload.pending = true;
    if (!prepareDestinationStructure(pages, destinationPlan, payload)) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }
    MacroHistoryChange* reserved = change.get();
    if (!parkPending_(std::move(change))) {
        failure.status = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
        return failure;
    }

    if (!applyMacroCreation(pages, address, payload)) {
        (void)takePending_();
        return failure;
    }

    auto binding = bindingDraft;
    binding.sourceId = sourceId;
    const auto bound = addProjectModulationBinding(graph, binding);
    if (!bound.changed()) {
        restoreCreationBefore(pages, address, payload, true);
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
        !creationIdentityMatches(pages, address, payload, false)) {
        return false;
    }
    restoreCreationBefore(pages, address, payload, true);
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
        !creationIdentityMatches(pages, address, payload, false)) {
        return false;
    }
    if (!applyDestinationStructure(pages, payload)) return false;
    const auto& graph = pages.control.authored.modulation;
    const auto* source = core::state::modulation::findProjectModulator(
        graph,
        audition.sourceId
    );
    if (source == nullptr) return false;
    payload.source = *source;
    payload.binding = graph.outputBindings[payload.beforeBindingCount];
    if (payload.triggerCreated) {
        payload.trigger = graph.triggerBindings[payload.beforeTriggerCount];
    }
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

FLASHMEM bool MacroHistoryService::setModulationDestinationScaleCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    uint16_t scaleQ15
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address)) return false;
    auto& graph = pages.control.authored.modulation;
    const auto destination = projectControlDestination(address);
    const uint16_t current = projectModulationDestinationScaleQ15(
        graph,
        destination
    );
    if (current == scaleQ15) return false;

    if (coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == MacroHistoryActionKind::GLOBAL_DEPTH_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->destinationScale.valid &&
            previous->destinationScale.destination == destination &&
            previous->destinationScale.afterScaleQ15 == current &&
            setProjectModulationDestinationScale(
                graph,
                destination,
                scaleQ15
            ).changed()) {
            pages.control.markAuthoredMutation();
            previous->destinationScale.afterScaleQ15 = scaleQ15;
            clearRedo_();
            return true;
        }
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::GLOBAL_DEPTH_EDIT;
    change->address = address;
    change->destinationScale = {
        .destination = destination,
        .beforeScaleQ15 = current,
        .afterScaleQ15 = scaleQ15,
        .valid = true,
    };
    if (!setProjectModulationDestinationScale(
            graph,
            destination,
            scaleQ15
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredMutation();
    endCoalescing();
    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    coalescing_ = true;
    coalesced_kind_ = MacroHistoryActionKind::GLOBAL_DEPTH_EDIT;
    coalesced_address_ = address;
    return true;
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

FLASHMEM bool MacroHistoryService::removeMacroSlot(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr ||
        !macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(
            address.macro
        )) {
        return false;
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->slotRemoval =
        core::app::makeExtmemUnique<MacroSlotRemovalHistoryPayload>();
    if (!change->slotRemoval) return false;
    change->kind = MacroHistoryActionKind::REMOVE_SLOT;
    change->address = address;
    auto& payload = *change->slotRemoval;
    if (!captureMacroSlotRemovalState(pages, address, payload.before)) {
        return false;
    }

    payload.after.automation.address = address;
    payload.after.modulation = payload.before.modulation;
    payload.after.modulation.globalBindingCount = static_cast<uint16_t>(
        payload.before.modulation.globalBindingCount -
        payload.before.modulation.assignmentCount
    );
    payload.after.modulation.assignmentCount = 0U;
    payload.after.modulation.destinationScaleQ15 =
        PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    payload.after.modulation.assignments = {};
    payload.after.macroActive = false;
    payload.after.cc = defaultMacroCc(address.page, address.macro);
    payload.after.staticValue = 0.5f;

    if (!liveMacroSlotRemovalStateMatches(pages, address, payload.before) ||
        !applyMacroSlotRemovalState(pages, address, payload.after) ||
        !liveMacroSlotRemovalStateMatches(pages, address, payload.after)) {
        if (!liveMacroSlotRemovalStateMatches(pages, address, payload.before)) {
            (void)applyMacroSlotRemovalState(pages, address, payload.before);
        }
        return false;
    }

    endCoalescing();
    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    return true;
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

FLASHMEM bool MacroHistoryService::setProjectModulatorName(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const char* name
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
    const auto result = core::state::modulation::setProjectModulatorName(
        pages.control.authored.modulation,
        sourceId,
        name
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

FLASHMEM bool MacroHistoryService::setProjectAdsrParametersCoalesced(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulatorAdsrParameters& parameters
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
        return false;
    }
    auto* source = findProjectModulator(
        pages.control.authored.modulation,
        sourceId
    );
    if (!source || source->kind != ModulatorKind::ADSR) return false;
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::PROJECT_MODULATOR_SOURCE_EDIT;
    change->sourceEdit.before = *source;
    const auto result = core::state::modulation::setProjectAdsrParameters(
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

FLASHMEM bool MacroHistoryService::setProjectModulationTriggerCoalesced(
    MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationTriggerRef& trigger,
    bool enabled
) {
    using namespace core::state::modulation;
    if (pendingModulatorSlot_() != nullptr || pages.control.audition.active) {
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
        enabled
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

    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    coalescing_ = true;
    coalesced_kind_ = MacroHistoryActionKind::PROJECT_MODULATOR_TRIGGER_EDIT;
    return true;
}

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
                pages,
                change->address,
                change->modulator,
                true
            )) {
            return false;
        }
        restoreCreationBefore(
            pages,
            change->address,
            change->modulator,
            false
        );
    } else if (change->modulatorSplit) {
        if (!restoreSplitBefore(pages, *change->modulatorSplit)) {
            return false;
        }
    } else if (change->modulatorDelete) {
        if (!restoreDeletedModulator(pages, *change->modulatorDelete)) {
            return false;
        }
    } else if (change->triggerEdit.valid) {
        auto* trigger =
            core::state::modulation::findProjectModulationTriggerForSource(
                pages.control.authored.modulation,
                change->triggerEdit.after.sourceId
            );
        if (trigger == nullptr ||
            !sameObjectBits(*trigger, change->triggerEdit.after)) {
            return false;
        }
        *trigger = change->triggerEdit.before;
        pages.control.markAuthoredMutation();
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
    } else if (change->destinationScale.valid) {
        auto& graph = pages.control.authored.modulation;
        const auto& scale = change->destinationScale;
        if (core::state::modulation::projectModulationDestinationScaleQ15(
                graph,
                scale.destination
            ) != scale.afterScaleQ15 ||
            !core::state::modulation::setProjectModulationDestinationScale(
                graph,
                scale.destination,
                scale.beforeScaleQ15
            ).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
    } else if (change->slotRemoval) {
        if (!liveMacroSlotRemovalStateMatches(
                pages,
                change->address,
                change->slotRemoval->after
            ) || !applyMacroSlotRemovalState(
                pages,
                change->address,
                change->slotRemoval->before
            )) {
            return false;
        }
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
    } else if (change->automationTake) {
        if (!liveAutomationTakeMatches(
                pages,
                *change->automationTake,
                true
            ) || !applyAutomationTakeAtomically(
                pages,
                *change->automationTake,
                false
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
        if (!creationBeforeMatches(
                pages,
                change->address,
                change->modulator
            )) {
            return false;
        }
        restoreCreationAfter(pages, change->address, change->modulator);
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
    } else if (change->triggerEdit.valid) {
        auto* trigger =
            core::state::modulation::findProjectModulationTriggerForSource(
                pages.control.authored.modulation,
                change->triggerEdit.before.sourceId
            );
        if (trigger == nullptr ||
            !sameObjectBits(*trigger, change->triggerEdit.before)) {
            return false;
        }
        *trigger = change->triggerEdit.after;
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
    } else if (change->destinationScale.valid) {
        auto& graph = pages.control.authored.modulation;
        const auto& scale = change->destinationScale;
        if (core::state::modulation::projectModulationDestinationScaleQ15(
                graph,
                scale.destination
            ) != scale.beforeScaleQ15 ||
            !core::state::modulation::setProjectModulationDestinationScale(
                graph,
                scale.destination,
                scale.afterScaleQ15
            ).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
    } else if (change->slotRemoval) {
        if (!liveMacroSlotRemovalStateMatches(
                pages,
                change->address,
                change->slotRemoval->before
            ) || !applyMacroSlotRemovalState(
                pages,
                change->address,
                change->slotRemoval->after
            )) {
            return false;
        }
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
    } else if (change->automationTake) {
        if (!liveAutomationTakeMatches(
                pages,
                *change->automationTake,
                false
            ) || !applyAutomationTakeAtomically(
                pages,
                *change->automationTake,
                true
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
