#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::macro {

/**
 * Bounded, Slot-scoped Macro history.
 *
 * A history entry stores only the addressed Slot and its packed curve points,
 * never the 128 KiB project point pool. The 4096-point admission limit covers
 * the maximum authored Automation plus Modulation pair (2 x 2048 points) and
 * makes memory use deterministic on the controller.
 */
inline constexpr uint16_t MACRO_HISTORY_POINT_CAPACITY =
    static_cast<uint16_t>(MACRO_AUTOMATION_RECORDING_MAX_POINTS * 2U);

enum class MacroHistoryActionKind : uint8_t {
    CONVERT_AUTOMATION = 0,
    PASTE_SLOT,
    PASTE_DESTINATION,
    PASTE_AUTOMATION,
    PASTE_MODULATION,
    CLEAR_AUTOMATION,
    CLEAR_MODULATION,
    REMOVE_SLOT,
    DEPTH_EDIT,
    GLOBAL_DEPTH_EDIT,
    SOURCE_STATE,
    CREATE_MODULATOR_ASSIGNMENT,
    REMOVE_MODULATOR_ASSIGNMENT,
    PROJECT_MODULATOR_SOURCE_EDIT,
    PROJECT_MODULATOR_TRIGGER_EDIT,
    CREATE_PROJECT_MODULATOR,
    SPLIT_PROJECT_MODULATOR,
    DELETE_PROJECT_MODULATOR,
    RECORD_AUTOMATION,
};

struct MacroSlotHistorySnapshot {
    MacroAutomationSlotAddress address{};
    bool macroActive = false;
    uint8_t cc = 0;
    float staticValue = 0.0f;
    bool slotPresent = false;
    MacroAutomationSlotState slot{};
    uint16_t automationPointCount = 0;
    uint16_t modulationPointCount = 0;
    uint16_t destinationScaleQ15 =
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    std::array<MacroPackedCurvePoint, MACRO_HISTORY_POINT_CAPACITY> points{};
};

struct MacroSlotHistoryChangePayload {
    MacroSlotHistorySnapshot before{};
    MacroSlotHistorySnapshot after{};
};

/**
 * Absolute-Automation-only snapshot.
 *
 * Point storage is exact-length PSRAM so recording history never retains a
 * Modulation source, binding, graph, curve arena, or fixed maximum point array.
 */
struct MacroAutomationHistorySnapshot {
    MacroAutomationSlotAddress address{};
    MacroAutomationCurveRef automation{};
    uint16_t pointCount = 0;
    core::app::ExtmemUniqueArray<MacroPackedCurvePoint> points{};
};

struct MacroAutomationHistoryPayload {
    MacroAutomationHistorySnapshot before{};
    MacroAutomationHistorySnapshot after{};
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
    core::app::ExtmemUniquePtr<MacroModulationAssignmentsHistoryPayload>
        modulationAssignments{};
    MacroDestinationScaleHistoryPayload destinationScale{};
    MacroModulatorCreationHistoryPayload modulator{};
    ProjectModulatorSourceHistoryPayload sourceEdit{};
    ProjectModulatorTriggerHistoryPayload triggerEdit{};
    core::app::ExtmemUniquePtr<ProjectModulatorDeleteHistoryPayload>
        modulatorDelete{};
    core::app::ExtmemUniquePtr<ProjectModulatorSplitHistoryPayload>
        modulatorSplit{};
};

using MacroHistoryChangePtr = core::app::ExtmemUniquePtr<MacroHistoryChange>;

[[nodiscard]] bool captureMacroSlotHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotHistorySnapshot& out
);

[[nodiscard]] bool sameMacroSlotHistorySnapshot(
    const MacroSlotHistorySnapshot& lhs,
    const MacroSlotHistorySnapshot& rhs
);

[[nodiscard]] bool liveMacroSlotMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
);

/** Applies a validated Slot snapshot without partially mutating on failure. */
[[nodiscard]] bool applyMacroSlotHistorySnapshot(
    MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
);

[[nodiscard]] bool captureMacroAutomationHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroAutomationHistorySnapshot& out
);

[[nodiscard]] bool sameMacroAutomationHistorySnapshot(
    const MacroAutomationHistorySnapshot& lhs,
    const MacroAutomationHistorySnapshot& rhs
);

[[nodiscard]] bool liveMacroAutomationMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
);

/** Applies only absolute Automation and preserves every Modulation object. */
[[nodiscard]] bool applyMacroAutomationHistorySnapshot(
    MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
);

class MacroHistoryService {
public:
    static constexpr uint8_t ENTRY_LIMIT = 8;

    MacroHistoryService();
    ~MacroHistoryService();
    MacroHistoryService(const MacroHistoryService&) = delete;
    MacroHistoryService& operator=(const MacroHistoryService&) = delete;

    [[nodiscard]] MacroHistoryChangePtr prepare(
        const MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        MacroHistoryActionKind kind
    ) const;

    /** Captures only absolute Automation for a performance recording commit. */
    [[nodiscard]] MacroHistoryChangePtr prepareAutomationRecording(
        const MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    ) const;

    /**
     * Captures the post-state and records one action. On capture/admission
     * failure the pre-state is restored before returning false.
     */
    [[nodiscard]] bool commitPrepared(
        MacroPagesState& pages,
        MacroHistoryChangePtr change,
        bool coalesce = false
    );

    /**
     * Reserves history before publishing one provisional LFO + assignment.
     * The returned IDs are also projected through ProjectControlState::audition.
     */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginLfoModulatorAudition(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::ModulatorLfoDraft& sourceDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot = false
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginAdsrModulatorAudition(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
            const core::state::modulation::ModulationTriggerDraft& triggerDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot = false
        );

    /** Creates one explicit detached LFO as one compact Undo action. */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createUnassignedLfo(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorLfoDraft& sourceDraft
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createUnassignedAdsr(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
            const core::state::modulation::ModulationTriggerDraft& triggerDraft
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        duplicateProjectModulator(
            MacroPagesState& pages,
            core::state::modulation::ModulatorId sourceId,
            const char* cloneName
        );

    /** Reserves and auditions one edge to a pre-existing Project source. */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginExistingModulatorAudition(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            core::state::modulation::ModulatorId sourceId,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            const core::state::modulation::ModulatorReach* widenedReach = nullptr,
            bool createMacroSlot = false
        );

    /** Exact rollback with no Undo entry and no authored ID/capacity residue. */
    [[nodiscard]] bool cancelModulatorAudition(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    /** Publishes the reserved delta as one stable-ID Undo action. */
    [[nodiscard]] bool commitModulatorAudition(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    [[nodiscard]] bool modulatorAuditionPending(
        const MacroAutomationSlotAddress& address
    ) const;

    /** Depth edit fast path: one allocation on first turn, none while coalescing. */
    [[nodiscard]] bool setModulationDepthCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        float depth
    );

    /** Signed Depth edit for one stable assignment; coalesced per gesture. */
    [[nodiscard]] bool setModulationBindingDepthCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        core::state::modulation::ModulationBindingId bindingId,
        float depth
    );

    /** Destination-wide 0..200% multiplier; coalesced without edge snapshots. */
    [[nodiscard]] bool setModulationDestinationScaleCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        uint16_t scaleQ15
    );

    [[nodiscard]] bool setModulationBindingEnabled(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        core::state::modulation::ModulationBindingId bindingId,
        bool enabled
    );

    [[nodiscard]] bool setAllModulationBindingsEnabled(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        bool enabled
    );

    [[nodiscard]] bool removeModulationBinding(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        core::state::modulation::ModulationBindingId bindingId
    );

    [[nodiscard]] bool clearModulationBindings(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    /** Adds or updates one typed shared-source assignment as one Undo action. */
    [[nodiscard]] bool pasteModulationBinding(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        const core::state::modulation::ModulationBindingDraft& draft,
        bool overwriteExisting,
        core::state::modulation::ModulationBindingId* appliedBinding = nullptr
    );

    [[nodiscard]] bool setProjectModulatorEnabled(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        bool enabled
    );
    [[nodiscard]] bool setProjectModulatorName(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const char* name
    );
    [[nodiscard]] bool setProjectLfoParametersCoalesced(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const core::state::modulation::ModulatorLfoParameters& parameters
    );
    [[nodiscard]] bool setProjectAdsrParametersCoalesced(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const core::state::modulation::ModulatorAdsrParameters& parameters
    );
    [[nodiscard]] bool setProjectModulationTriggerCoalesced(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const core::state::modulation::ModulationTriggerRef& trigger,
        bool enabled
    );
    [[nodiscard]] bool setProjectModulatorReach(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const core::state::modulation::ModulatorReach& reach
    );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        splitProjectModulator(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorSplitRequest& request
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        splitProjectModulatorTrack(
            MacroPagesState& pages,
            core::state::modulation::ModulatorId sourceId,
            uint8_t track,
            const char* cloneName,
            const core::state::modulation::ModulatorReach& retainedReach,
            const core::state::modulation::ModulatorReach& cloneReach
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        deleteProjectModulator(
            MacroPagesState& pages,
            core::state::modulation::ModulatorId sourceId
        );

    void endCoalescing();
    [[nodiscard]] bool undo(
        MacroPagesState& pages,
        MacroAutomationSlotAddress* appliedAddress = nullptr
    );
    [[nodiscard]] bool redo(
        MacroPagesState& pages,
        MacroAutomationSlotAddress* appliedAddress = nullptr
    );
    void clear();

    [[nodiscard]] bool canUndo() const { return undo_count_ > 0; }
    [[nodiscard]] bool canRedo() const { return redo_count_ > 0; }
    [[nodiscard]] uint8_t undoCount() const { return undo_count_; }
    [[nodiscard]] uint8_t redoCount() const { return redo_count_; }

private:
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginNewModulatorAudition_(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::ModulatorLfoDraft* lfoDraft,
            const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
            const core::state::modulation::ModulationTriggerDraft* triggerDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createUnassignedModulator_(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorLfoDraft* lfoDraft,
            const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
            const core::state::modulation::ModulationTriggerDraft* triggerDraft
        );
    [[nodiscard]] MacroHistoryChangePtr prepareModulationAssignments_(
        const MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        MacroHistoryActionKind kind
    ) const;
    [[nodiscard]] bool commitModulationAssignments_(
        MacroPagesState& pages,
        MacroHistoryChangePtr change,
        bool coalesce = false
    );
    [[nodiscard]] bool commitProjectSourceEdit_(
        MacroPagesState& pages,
        MacroHistoryChangePtr change,
        bool coalesce
    );
    [[nodiscard]] MacroHistoryChangePtr* pendingModulatorSlot_();
    [[nodiscard]] const MacroHistoryChangePtr* pendingModulatorSlot_() const;
    [[nodiscard]] bool parkPending_(MacroHistoryChangePtr change);
    [[nodiscard]] MacroHistoryChangePtr takePending_();
    static void push_(
        std::array<MacroHistoryChangePtr, ENTRY_LIMIT>& stack,
        uint8_t& count,
        MacroHistoryChangePtr change
    );
    void clearRedo_();

    std::array<MacroHistoryChangePtr, ENTRY_LIMIT> undo_{};
    std::array<MacroHistoryChangePtr, ENTRY_LIMIT> redo_{};
    uint8_t undo_count_ = 0;
    uint8_t redo_count_ = 0;
    bool coalescing_ = false;
    MacroHistoryActionKind coalesced_kind_ = MacroHistoryActionKind::SOURCE_STATE;
    MacroAutomationSlotAddress coalesced_address_{};
};

}  // namespace core::state::macro
