#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "state/macro/MacroHistory.hpp"

namespace core::state::macro::history_detail {

struct HistoryHash64Result {
    uint64_t value = 14695981039346656037ULL;
    bool valid = false;
};

template <typename T>
bool sameObjectBits(const T& lhs, const T& rhs) {
    static_assert(std::is_trivially_copyable_v<T>);
    return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

bool sameCurveMetadata(
    const core::state::modulation::ProjectControlCurvePayload& lhs,
    const core::state::modulation::ProjectControlCurvePayload& rhs
);

bool sameCurveMetadata(
    const core::state::modulation::ProjectControlCurveView& lhs,
    const core::state::modulation::ProjectControlCurvePayload& rhs
);

bool sameCurveSpec(
    const core::state::modulation::ProjectCurveSpec& lhs,
    const core::state::modulation::ProjectCurveSpec& rhs
);

bool captureAutomationMetadataHistory(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroAutomationMetadataHistoryPayload& out
);

bool liveAutomationMetadataMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroAutomationMetadataHistoryPayload& payload,
    bool after,
    bool verifyPoints
);

bool applyAutomationMetadataHistory(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroAutomationMetadataHistoryPayload& payload,
    bool after
);

bool sameFloatBits(float lhs, float rhs);

bool samePoint(
    const core::state::modulation::ProjectPackedCurvePoint& lhs,
    const core::state::modulation::ProjectPackedCurvePoint& rhs
);

uint16_t snapshotPointCount(const MacroSlotHistorySnapshot& snapshot);

bool snapshotConsistent(const MacroSlotHistorySnapshot& snapshot);

bool automationSnapshotConsistent(
    const MacroAutomationHistorySnapshot& snapshot
);

bool automationTakePayloadConsistent(
    const MacroAutomationTakeHistoryPayload& payload,
    bool requireTouched
);

bool liveAutomationTakeMatches(
    const MacroPagesState& pages,
    const MacroAutomationTakeHistoryPayload& payload,
    bool after
);

bool applyAutomationTakeAtomically(
    MacroPagesState& pages,
    const MacroAutomationTakeHistoryPayload& payload,
    bool after
);

void normalizeCurveOffsets(MacroSlotHistorySnapshot& snapshot);

bool liveProjectCurveMatches(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ProjectControlCurveView& live,
    const core::state::modulation::ProjectControlCurvePayload& expected,
    const MacroSlotHistorySnapshot& snapshot,
    uint16_t snapshotOffset
);

bool sameAddress(
    const MacroAutomationSlotAddress& lhs,
    const MacroAutomationSlotAddress& rhs
);

void readManualOverride(
    const MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool& active,
    float& value
);

bool manualOverrideMatches(
    const MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool expectedActive,
    float expectedValue
);

bool canApplyManualOverride(
    const MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool targetActive
);

bool applyManualOverride(
    MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool targetActive,
    float targetValue
);

uint32_t hashBytes(uint32_t hash, const void* data, size_t size);

bool destinationScaleRemovedWithSource(
    const core::state::modulation::ProjectModulationState& graph,
    const core::state::modulation::ModulationDestination& destination,
    core::state::modulation::ModulatorId sourceId
);

uint32_t unrelatedModulatorHash(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
);

uint64_t hashBytes64(uint64_t hash, const void* data, size_t size);

bool historyGraphRangesValid(
    const core::state::modulation::ProjectModulationState& graph
);

bool historyArenaRangesValid(
    const core::state::modulation::ProjectCurveArena& arena
);

bool historyDomainValid(
    const core::state::modulation::ProjectControlDomainState& domain
);

HistoryHash64Result recordedShapeGraphHash(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
);

int16_t historyCurveIndex(
    const core::state::modulation::ProjectCurveArena& arena,
    core::state::modulation::ProjectCurveId id
);

core::state::modulation::ProjectCurveSpec historyCurveSpec(
    const core::state::modulation::ProjectCurveRecord& record
);

uint32_t historyNextStableId(uint32_t current);

bool historyCurvePointsMatch(
    const core::state::modulation::ProjectCurveArena& arena,
    const core::state::modulation::ProjectCurveRecord& record,
    const core::state::modulation::ProjectPackedCurvePoint* expected,
    uint16_t count
);

bool historyArenaRangeMatches(
    const core::state::modulation::ProjectCurveArena& arena,
    uint16_t offset,
    const core::state::modulation::ProjectPackedCurvePoint* expected,
    uint16_t count
);

HistoryHash64Result unrelatedCurveHash(
    const core::state::modulation::ProjectControlDomainState& domain,
    core::state::modulation::ProjectCurveId excludedA,
    core::state::modulation::ProjectCurveId excludedB = {}
);

bool recordedCreationStorageValid(
    const ProjectRecordedShapeCreationHistoryPayload& payload
);

bool recordedCreationMatches(
    const core::state::modulation::ProjectControlState& control,
    const ProjectRecordedShapeCreationHistoryPayload& payload,
    bool after
);

void restoreRecordedCreation(
    core::state::modulation::ProjectControlState& control,
    const ProjectRecordedShapeCreationHistoryPayload& payload,
    bool after
);

bool recordedShapeEditStorageValid(
    const ProjectRecordedShapeEditHistoryPayload& payload
);

bool recordedShapeEditMatches(
    const MacroPagesState& pages,
    const ProjectRecordedShapeEditHistoryPayload& payload,
    bool after
);

bool applyRecordedShapeEdit(
    MacroPagesState& pages,
    const ProjectRecordedShapeEditHistoryPayload& payload,
    bool after
);

template <typename Entry, size_t Capacity>
bool insertDenseHistoryEntry(
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

bool deleteAfterMatches(
    const MacroPagesState& pages,
    const ProjectModulatorDeleteHistoryPayload& payload
);

bool deleteBeforeMatches(
    const MacroPagesState& pages,
    const ProjectModulatorDeleteHistoryPayload& payload
);

bool restoreDeletedModulator(
    MacroPagesState& pages,
    const ProjectModulatorDeleteHistoryPayload& payload
);

bool macroCreationStateMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool after
);

bool sameMacroTrackData(
    const MacroTrackData& lhs,
    const MacroTrackData& rhs
);

bool destinationStructureMatches(
    const MacroPagesState& pages,
    const MacroDestinationStructureHistoryPayload& payload,
    bool after
);

bool prepareDestinationStructure(
    const MacroPagesState& pages,
    const MacroDestinationActivationPlan* plan,
    MacroModulatorCreationHistoryPayload& payload
);

bool applyDestinationStructure(
    MacroPagesState& pages,
    MacroModulatorCreationHistoryPayload& payload
);

void restoreDestinationStructure(
    MacroPagesState& pages,
    const MacroModulatorCreationHistoryPayload& payload,
    bool after
);

void restoreMacroCreationState(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool after
);

bool applyMacroCreation(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroModulatorCreationHistoryPayload& payload
);

bool creationIdentityMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactAfter
);

bool creationBeforeMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload
);

void restoreCreationBefore(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload,
    bool exactCancel
);

void restoreCreationAfter(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroModulatorCreationHistoryPayload& payload
);

bool splitPayloadStorageValid(
    const ProjectModulatorSplitHistoryPayload& payload
);

bool splitCurveReferenceMatches(
    const core::state::modulation::ProjectControlState& control,
    const ProjectModulatorSplitHistoryPayload& payload,
    bool after
);

bool splitBeforeMatches(
    const MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
);

bool splitAfterMatches(
    const MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
);

bool restoreSplitBefore(
    MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
);

bool restoreSplitAfter(
    MacroPagesState& pages,
    const ProjectModulatorSplitHistoryPayload& payload
);

uint32_t auditionGeneration(
    uint32_t revision,
    core::state::modulation::ModulatorId sourceId,
    core::state::modulation::ModulationBindingId bindingId
);

uint32_t hashBinding(uint32_t hash,
                              const core::state::modulation::ModulationBindingState& binding);

uint32_t unrelatedBindingHash(
    const core::state::modulation::ProjectModulationState& graph,
    const core::state::modulation::ModulationDestination& destination
);

bool captureModulationAssignments(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroModulationAssignmentSnapshot& out
);

bool sameModulationAssignments(
    const MacroModulationAssignmentSnapshot& lhs,
    const MacroModulationAssignmentSnapshot& rhs
);

bool liveModulationAssignmentsMatch(
    const MacroPagesState& pages,
    const MacroModulationAssignmentSnapshot& expected
);

bool applyModulationAssignmentsToGraph(
    core::state::modulation::ProjectModulationState& graph,
    const MacroModulationAssignmentSnapshot& target
);

bool applyModulationAssignments(
    MacroPagesState& pages,
    const MacroModulationAssignmentSnapshot& target
);

bool captureMacroSlotRemovalState(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotRemovalState& out
);

bool liveMacroSlotRemovalStateMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroSlotRemovalState& expected
);

bool applyMacroSlotRemovalState(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroSlotRemovalState& target
);

uint64_t pageStructureControlHash(
    const core::state::modulation::ProjectControlDomainState& domain
);

void syncPageStructureTrack(
    MacroPagesState& pages,
    uint8_t track
);

bool pageStructureBeforeMatches(
    const MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload
);

bool pageStructureAfterMatches(
    const MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload
);

bool applyPageStructureHistory(
    MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload,
    bool after
);

}  // namespace core::state::macro::history_detail
