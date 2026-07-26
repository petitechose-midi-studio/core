#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroAutomationAddress.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroRuntimeState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectHistoryEventSink.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::state::macro {

/**
 * Bounded Macro history. Ordinary entries remain Slot-scoped; the rare Page
 * compaction entry owns one exact pre-transaction Project domain in PSRAM so
 * structural Undo can restore deleted curves and assignments atomically.
 *
 * A history entry stores only the addressed Slot and its packed curve points,
 * never the 128 KiB project point pool. The 4096-point admission limit covers
 * the maximum authored Automation plus Modulation pair (2 x 2048 points) and
 * makes memory use deterministic on the controller.
 */
inline constexpr uint16_t MACRO_HISTORY_POINT_CAPACITY =
    static_cast<uint16_t>(MACRO_AUTOMATION_RECORDING_MAX_POINTS * 2U);

/**
 * One directly recorded Project source is intentionally bounded to the same
 * 2048 points as one authored Automation take. History owns exact-length
 * PSRAM arrays; this constant is an admission limit, not reserved RAM1.
 */
inline constexpr uint16_t RECORDED_SHAPE_HISTORY_POINT_CAPACITY =
    MACRO_AUTOMATION_RECORDING_MAX_POINTS;

enum class MacroHistoryActionKind : uint8_t {
    CONVERT_AUTOMATION = 0,
    PASTE_SLOT,
    PASTE_DESTINATION,
    PASTE_AUTOMATION,
    PASTE_MODULATION,
    CLEAR_AUTOMATION,
    CLEAR_MODULATION,
    PAGE_STRUCTURE,
    REMOVE_SLOT,
    DEPTH_EDIT,
    GLOBAL_DEPTH_EDIT,
    SOURCE_STATE,
    AUTOMATION_STATE,
    CREATE_MODULATOR_ASSIGNMENT,
    REMOVE_MODULATOR_ASSIGNMENT,
    PROJECT_MODULATOR_SOURCE_EDIT,
    PROJECT_MODULATOR_TRIGGER_EDIT,
    CREATE_PROJECT_MODULATOR,
    SPLIT_PROJECT_MODULATOR,
    DELETE_PROJECT_MODULATOR,
    RECORD_AUTOMATION,
    STATIC_VALUE_EDIT,
    CREATE_SLOT,
    AUTOMATION_DURATION_EDIT,
    AUTOMATION_WINDOW_EDIT,
    CONFIG_EDIT,
    MANUAL_OVERRIDE_STATE,
};

struct MacroSlotHistorySnapshot {
    MacroAutomationSlotAddress address{};
    bool macroActive = false;
    uint8_t cc = 0;
    float staticValue = 0.0f;
    bool slotPresent = false;
    core::state::modulation::ProjectControlMacroDestinationPayload control{};
    uint16_t automationPointCount = 0;
    uint16_t modulationPointCount = 0;
    uint16_t destinationScaleQ15 =
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    std::array<
        core::state::modulation::ProjectPackedCurvePoint,
        MACRO_HISTORY_POINT_CAPACITY
    > points{};
};

struct MacroSlotHistoryChangePayload {
    MacroSlotHistorySnapshot before{};
    MacroSlotHistorySnapshot after{};
};

/** Compact continuous edit for one authored Macro base value. */
struct MacroValueHistoryPayload {
    float before = 0.5f;
    float after = 0.5f;
    bool valid = false;
};

/**
 * Address-scoped runtime authority retained beside an optional Base edit.
 *
 * A physical takeover can change both the durable Macro Base and the
 * session-only Automation/Manual authority. Keeping both sides in one entry
 * makes one gesture exactly one Undo, while preserving unrelated overrides.
 */
struct MacroManualOverrideHistoryPayload {
    float beforeValue = 0.0f;
    float afterValue = 0.0f;
    bool beforeActive = false;
    bool afterActive = false;
    bool valid = false;
};

/** Canonical Project Track side of a combined Channel + Macro CC edit. */
struct MacroTrackRoutingHistoryPayload {
    core::state::project::ProjectTrackSnapshot before{};
    core::state::project::ProjectTrackSnapshot after{};
    bool valid = false;
};

/** One all-eight-Macro routing import on one physical Page. */
struct MacroTrackConfigHistoryPayload {
    core::state::project::ProjectTrackSnapshot beforeTracks{};
    core::state::project::ProjectTrackSnapshot afterTracks{};
    std::array<uint8_t, MACRO_COUNT> beforeCc{};
    std::array<uint8_t, MACRO_COUNT> afterCc{};
    uint8_t track = 0U;
    uint8_t page = 0U;
    bool valid = false;
};

/** Rare cross-runtime/cross-domain payload kept out of the hot entry body. */
struct MacroAuxiliaryHistoryPayload {
    MacroManualOverrideHistoryPayload manualOverride{};
    MacroTrackRoutingHistoryPayload trackRouting{};
    MacroTrackConfigHistoryPayload trackConfig{};
};

/**
 * Absolute-Automation-only snapshot.
 *
 * Point storage is exact-length PSRAM so recording history never retains a
 * Modulation source, binding, graph, curve arena, or fixed maximum point array.
 */
struct MacroAutomationHistorySnapshot {
    MacroAutomationSlotAddress address{};
    core::state::modulation::ProjectControlCurvePayload automation{};
    uint16_t pointCount = 0;
    core::app::ExtmemUniqueArray<
        core::state::modulation::ProjectPackedCurvePoint
    > points{};
};

struct MacroAutomationHistoryPayload {
    MacroAutomationHistorySnapshot before{};
    MacroAutomationHistorySnapshot after{};
};

inline constexpr uint8_t MACRO_AUTOMATION_TAKE_DESTINATION_CAPACITY =
    MACRO_COUNT;

/**
 * One multi-destination Automation history command.
 *
 * Before arrays are exact-length. Each after array reserves the recording
 * maximum before t0, in PSRAM, so commit and Redo need no capture allocation.
 */
struct MacroAutomationTakeHistoryPayload {
    std::array<
        MacroAutomationHistorySnapshot,
        MACRO_AUTOMATION_TAKE_DESTINATION_CAPACITY
    > before{};
    std::array<
        MacroAutomationHistorySnapshot,
        MACRO_AUTOMATION_TAKE_DESTINATION_CAPACITY
    > after{};
    uint16_t candidateMask = 0U;
    uint16_t touchedMask = 0U;
    uint8_t track = 0U;
    uint8_t page = 0U;
    std::array<uint8_t, 2> reserved{};
};

/**
 * Exact cold snapshot for topology created by one destination assignment.
 *
 * The complete target Macro Track is retained in PSRAM only when a missing
 * Track, Page, or Macro position is requested. This keeps ordinary assignment
 * history compact while making Cancel/Undo/Redo byte-stable for hidden state.
 */
struct MacroDestinationStructureHistoryPayload {
    MacroDestinationActivationPlan plan{};
    MacroTrackData beforeTrack{};
    MacroTrackData afterTrack{};
    uint16_t beforeTrackEnabledMask = MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    uint16_t afterTrackEnabledMask = MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    uint8_t beforeActiveTrack = 0U;
    uint8_t afterActiveTrack = 0U;
    bool applied = false;
};

/**
 * Exact append delta for one newly owned Recorded Shape curve.
 *
 * The inactive tail bytes are retained because the packed arena deliberately
 * does not clear released storage. Keeping only the overwritten range makes
 * Undo byte-exact without snapshotting the complete 137 kB arena.
 */
struct ProjectRecordedShapeCreationHistoryPayload {
    core::state::modulation::ProjectCurveRecord beforeRecordTail{};
    core::state::modulation::ProjectCurveRecord curve{};
    uint32_t beforeNextCurveId = 1U;
    uint32_t afterNextCurveId = 1U;
    uint64_t unrelatedCurveHash = 0U;
    uint16_t beforeRecordCount = 0U;
    uint16_t beforePointCount = 0U;
    uint16_t pointCount = 0U;
    bool unrelatedCurveHashValid = false;
    bool valid = false;
    core::app::ExtmemUniqueArray<
        core::state::modulation::ProjectPackedCurvePoint
    > points{};
    core::app::ExtmemUniqueArray<
        core::state::modulation::ProjectPackedCurvePoint
    > beforePointTail{};
};

/**
 * Small graph delta for destination-first source creation.
 *
 * The before-tail values make Cancel byte-stable even when a dense directory
 * slot was previously used. The committed after objects are sufficient for
 * stable-ID Undo/Redo and avoid retaining the complete Project graph.
 */
struct MacroModulatorCreationHistoryPayload {
    core::state::modulation::ModulatorSourceState beforeSourceTail{};
    core::state::modulation::ModulatorSourceState beforeSource{};
    core::state::modulation::ModulationBindingState beforeBindingTail{};
    core::state::modulation::ModulationTriggerBindingState beforeTriggerTail{};
    core::state::modulation::ModulatorSourceState source{};
    core::state::modulation::ModulationBindingState binding{};
    core::state::modulation::ModulationTriggerBindingState trigger{};
    core::state::modulation::ProjectCurveId sharedCurveId{};
    uint32_t beforeNextSourceId = 1;
    uint32_t beforeNextBindingId = 1;
    uint32_t afterNextSourceId = 1;
    uint32_t afterNextBindingId = 1;
    uint32_t beforeAuthoredRevision = 1;
    uint32_t generation = 0;
    uint16_t beforeSourceCount = 0;
    uint16_t beforeBindingCount = 0;
    uint16_t beforeTriggerCount = 0;
    uint16_t beforeSharedCurveReferenceCount = 0;
    float beforeMacroValue = 0.5f;
    float afterMacroValue = 0.5f;
    uint8_t beforeMacroActiveMask = 0;
    uint8_t afterMacroActiveMask = 0;
    uint8_t beforeMacroCc = 0;
    uint8_t afterMacroCc = 0;
    bool sourceCreated = false;
    bool bindingCreated = false;
    bool triggerCreated = false;
    bool sharedCurveReferenceCreated = false;
    bool macroCreated = false;
    bool pending = false;
    core::app::ExtmemUniquePtr<ProjectRecordedShapeCreationHistoryPayload>
        recordedShape{};
    core::app::ExtmemUniquePtr<MacroDestinationStructureHistoryPayload>
        destinationStructure{};
};

inline constexpr uint16_t MACRO_MODULATION_ASSIGNMENT_CAPACITY =
    core::state::modulation::PROJECT_MODULATOR_CAPACITY;

struct MacroModulationAssignmentSnapshotEntry {
    core::state::modulation::ModulationBindingState binding{};
    uint16_t globalIndex = 0;
    uint16_t reserved = 0;
};

/**
 * Destination-scoped graph snapshot used by edge edits and aggregate bypass.
 *
 * A Macro can receive at most one edge from each Project source, hence the
 * 128-entry bound. Global positions plus an unrelated-state hash make Undo
 * restore stable list order without retaining the complete Project graph.
 */
struct MacroModulationAssignmentSnapshot {
    core::state::modulation::ModulationDestination destination{};
    uint32_t nextBindingId = 1;
    uint32_t unrelatedHash = 0;
    uint16_t globalBindingCount = 0;
    uint16_t assignmentCount = 0;
    uint16_t destinationScaleQ15 =
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    std::array<
        MacroModulationAssignmentSnapshotEntry,
        MACRO_MODULATION_ASSIGNMENT_CAPACITY
    > assignments{};
};

struct MacroModulationAssignmentsHistoryPayload {
    MacroModulationAssignmentSnapshot before{};
    MacroModulationAssignmentSnapshot after{};
};

/** Exact destination removal state; all large/cold members live in PSRAM. */
struct MacroSlotRemovalState {
    MacroAutomationHistorySnapshot automation{};
    MacroModulationAssignmentSnapshot modulation{};
    bool macroActive = false;
    uint8_t cc = 0U;
    float staticValue = 0.5f;
};

struct MacroSlotRemovalHistoryPayload {
    MacroSlotRemovalState before{};
    MacroSlotRemovalState after{};
};

/**
 * One compacted Macro Page transaction.
 *
 * Redo deterministically reapplies retainedPageMask, so only the exact before
 * Project domain is retained. Eight worst-case entries consume about 1.22 MiB
 * of PSRAM instead of retaining before/after copies of the 156 KiB domain.
 */
enum class MacroPageStructureHistoryOperation : uint8_t {
    COMPACT = 0,
    SNAPSHOT,
};

struct MacroPageStructureHistoryPayload {
    uint16_t retainedPageMask = 0U;
    uint8_t track = 0U;
    MacroPageStructureHistoryOperation operation =
        MacroPageStructureHistoryOperation::COMPACT;
    uint64_t afterControlHash = 0U;
    MacroTrackData beforeTrack{};
    MacroTrackData afterTrack{};
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > beforeControl{};
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > afterControl{};
};

/** Compact destination-wide Depth delta; no binding array is retained. */
struct MacroDestinationScaleHistoryPayload {
    core::state::modulation::ModulationDestination destination{};
    uint16_t beforeScaleQ15 =
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    uint16_t afterScaleQ15 =
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    bool valid = false;
};

/** One root-source edit; no graph or curve-arena snapshot is retained. */
struct ProjectModulatorSourceHistoryPayload {
    core::state::modulation::ModulatorSourceState before{};
    core::state::modulation::ModulatorSourceState after{};
    bool valid = false;
};

/**
 * Exact curve delta for re-recording one Project Recorded Shape source.
 *
 * Unique edits retain the stable curve ID and use before/after point arrays.
 * Shared edits retain the old record and one appended COW record. The small
 * boundary array stores only arena bytes made inactive by a size change (or
 * overwritten by the COW append), never the complete arena.
 */
struct ProjectRecordedShapeEditHistoryPayload {
    core::state::modulation::ModulatorSourceState beforeSource{};
    core::state::modulation::ModulatorSourceState afterSource{};
    core::state::modulation::ProjectCurveRecord beforeCurve{};
    core::state::modulation::ProjectCurveRecord afterPreviousCurve{};
    core::state::modulation::ProjectCurveRecord afterCurve{};
    core::state::modulation::ProjectCurveRecord beforeRecordTail{};
    uint32_t beforeNextCurveId = 1U;
    uint32_t afterNextCurveId = 1U;
    uint64_t unrelatedGraphHash = 0U;
    uint64_t unrelatedCurveHash = 0U;
    uint16_t beforeRecordCount = 0U;
    uint16_t afterRecordCount = 0U;
    uint16_t beforeArenaPointCount = 0U;
    uint16_t afterArenaPointCount = 0U;
    uint16_t beforeCurveIndex = 0U;
    uint16_t afterCurveIndex = 0U;
    uint16_t boundaryPointCount = 0U;
    bool unrelatedGraphHashValid = false;
    bool unrelatedCurveHashValid = false;
    bool copyOnWrite = false;
    bool valid = false;
    core::app::ExtmemUniqueArray<
        core::state::modulation::ProjectPackedCurvePoint
    > beforePoints{};
    core::app::ExtmemUniqueArray<
        core::state::modulation::ProjectPackedCurvePoint
    > afterPoints{};
    core::app::ExtmemUniqueArray<
        core::state::modulation::ProjectPackedCurvePoint
    > boundaryPoints{};
};

/** One stable-ID typed trigger edit; source and graph order remain untouched. */
struct ProjectModulatorTriggerHistoryPayload {
    core::state::modulation::ModulationTriggerBindingState before{};
    core::state::modulation::ModulationTriggerBindingState after{};
    bool valid = false;
};

struct ProjectModulatorDeleteBindingEntry {
    core::state::modulation::ModulationBindingState binding{};
    uint16_t globalIndex = 0;
};

struct ProjectModulatorDeleteTriggerEntry {
    core::state::modulation::ModulationTriggerBindingState trigger{};
    uint16_t globalIndex = 0;
};

struct ProjectModulatorDeleteScaleEntry {
    core::state::modulation::ModulationDestinationScaleState scale{};
};

struct ProjectModulatorSplitBindingEntry {
    core::state::modulation::ModulationBindingState before{};
    core::state::modulation::ModulationBindingState after{};
    uint16_t globalIndex = 0;
};

/**
 * Exact cold delta for one root-source Split.
 *
 * Split never copies curve points: a recorded source adds one immutable curve
 * reference. Only the moved output edges are retained in an exact-length PSRAM
 * array, so a one-destination Split does not reserve the 512-edge maximum.
 */
struct ProjectModulatorSplitHistoryPayload {
    core::state::modulation::ModulatorSourceState retainedBefore{};
    core::state::modulation::ModulatorSourceState retainedAfter{};
    core::state::modulation::ModulatorSourceState beforeSourceTail{};
    core::state::modulation::ModulatorSourceState clone{};
    core::state::modulation::ModulationTriggerBindingState beforeTriggerTail{};
    core::state::modulation::ModulationTriggerBindingState cloneTrigger{};
    core::state::modulation::ProjectCurveId sharedCurveId{};
    uint32_t beforeNextSourceId = 1;
    uint32_t beforeNextBindingId = 1;
    uint32_t afterNextSourceId = 1;
    uint32_t afterNextBindingId = 1;
    uint16_t sourceIndex = 0;
    uint16_t beforeSourceCount = 0;
    uint16_t beforeBindingCount = 0;
    uint16_t beforeTriggerCount = 0;
    uint16_t movedBindingCount = 0;
    uint16_t beforeSharedCurveReferenceCount = 0;
    bool triggerCreated = false;
    bool sharedCurveReferenceCreated = false;
    core::app::ExtmemUniqueArray<ProjectModulatorSplitBindingEntry>
        movedBindings{};
};

/** Cold source-scoped delete delta; never snapshots the complete graph/arena. */
struct ProjectModulatorDeleteHistoryPayload {
    core::state::modulation::ModulatorSourceState source{};
    core::state::modulation::ProjectCurveRecord curve{};
    uint32_t nextSourceId = 1;
    uint32_t nextBindingId = 1;
    uint32_t nextCurveId = 1;
    uint32_t unrelatedHash = 0;
    uint16_t sourceIndex = 0;
    uint16_t beforeSourceCount = 0;
    uint16_t beforeBindingCount = 0;
    uint16_t beforeTriggerCount = 0;
    uint16_t beforeScaleCount = 0;
    uint16_t beforeCurveRecordCount = 0;
    uint16_t beforeCurveArenaPointCount = 0;
    uint16_t curveRecordIndex = 0;
    uint16_t bindingCount = 0;
    uint16_t triggerCount = 0;
    uint16_t scaleCount = 0;
    uint16_t curvePointCount = 0;
    bool curvePresent = false;
    bool curveShared = false;
    core::app::ExtmemUniqueArray<ProjectModulatorDeleteBindingEntry> bindings{};
    core::app::ExtmemUniqueArray<ProjectModulatorDeleteTriggerEntry> triggers{};
    core::app::ExtmemUniqueArray<ProjectModulatorDeleteScaleEntry> scales{};
    core::app::ExtmemUniqueArray<
        core::state::modulation::ProjectPackedCurvePoint
    > curvePoints{};
};

struct MacroHistoryChange {
    MacroHistoryActionKind kind = MacroHistoryActionKind::SOURCE_STATE;
    MacroAutomationSlotAddress address{};
    core::app::ExtmemUniquePtr<MacroSlotHistoryChangePayload> slot{};
    core::app::ExtmemUniquePtr<MacroAutomationHistoryPayload> automation{};
    core::app::ExtmemUniquePtr<MacroAutomationTakeHistoryPayload>
        automationTake{};
    core::app::ExtmemUniquePtr<MacroModulationAssignmentsHistoryPayload>
        modulationAssignments{};
    core::app::ExtmemUniquePtr<MacroSlotRemovalHistoryPayload> slotRemoval{};
    core::app::ExtmemUniquePtr<MacroPageStructureHistoryPayload>
        pageStructure{};
    MacroDestinationScaleHistoryPayload destinationScale{};
    MacroModulatorCreationHistoryPayload modulator{};
    ProjectModulatorSourceHistoryPayload sourceEdit{};
    core::app::ExtmemUniquePtr<ProjectRecordedShapeEditHistoryPayload>
        recordedShapeEdit{};
    ProjectModulatorTriggerHistoryPayload triggerEdit{};
    MacroValueHistoryPayload valueEdit{};
    core::app::ExtmemUniquePtr<MacroAuxiliaryHistoryPayload> auxiliary{};
    core::app::ExtmemUniquePtr<ProjectModulatorDeleteHistoryPayload>
        modulatorDelete{};
    core::app::ExtmemUniquePtr<ProjectModulatorSplitHistoryPayload>
        modulatorSplit{};

    ~MacroHistoryChange();
};

using MacroHistoryChangePtr = core::app::ExtmemUniquePtr<MacroHistoryChange>;

}  // namespace core::state::macro
