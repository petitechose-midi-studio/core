#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/project/ProjectHistoryEventSink.hpp"

namespace core::state::sequencer {

using SequencerHistoryGraphPtr =
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>;
using SequencerHistoryCcLanePtr = SequencerCcLaneBankPtr;

struct SequencerHistoryPatternSnapshot {
    SequencerPatternSnapshot flat{};
    uint8_t focusedStep = 0;
    SequencerHistoryGraphPtr graph;
    SequencerHistoryCcLanePtr ccLanes;
    bool ccLanesCaptured = false;

    SequencerHistoryPatternSnapshot();
    ~SequencerHistoryPatternSnapshot();
    SequencerHistoryPatternSnapshot(const SequencerHistoryPatternSnapshot&) = delete;
    SequencerHistoryPatternSnapshot& operator=(const SequencerHistoryPatternSnapshot&) = delete;
    SequencerHistoryPatternSnapshot(SequencerHistoryPatternSnapshot&&) noexcept;
    SequencerHistoryPatternSnapshot& operator=(SequencerHistoryPatternSnapshot&&) noexcept;
    void reset();
};

struct SequencerHistoryTrackBankSnapshot {
    SequencerTrackBankSnapshot flat{};
    uint8_t focusedStep = 0;
    StepProperty activeStepProperty = StepProperty::NOTE;
    SequencerHistoryGraphPtr editorGraph;
    std::array<SequencerHistoryGraphPtr, SequencerTrackBankState::TRACK_COUNT> bankGraphs{};
    SequencerHistoryCcLanePtr editorCcLanes;
    std::array<SequencerHistoryCcLanePtr, SequencerTrackBankState::TRACK_COUNT>
        bankCcLanes{};

    SequencerHistoryTrackBankSnapshot();
    ~SequencerHistoryTrackBankSnapshot();
    SequencerHistoryTrackBankSnapshot(const SequencerHistoryTrackBankSnapshot&) = delete;
    SequencerHistoryTrackBankSnapshot& operator=(const SequencerHistoryTrackBankSnapshot&) = delete;
    SequencerHistoryTrackBankSnapshot(SequencerHistoryTrackBankSnapshot&&) noexcept;
    SequencerHistoryTrackBankSnapshot& operator=(SequencerHistoryTrackBankSnapshot&&) noexcept;
    void reset();
};

struct SequencerHistoryTrackStructureChange;
struct SequencerHistoryMacroTrackStructurePayload;

enum class SequencerHistoryScope : uint8_t {
    PatternOnly = 0,
    Structure,
    FullBank,
};

enum class SequencerHistoryPatternStorage : uint8_t {
    FullGraph = 0,
    // Restores flat pattern data while retaining the graph already owned by
    // the editor/track. Recording rejects entries whose graph revisions differ.
    FlatOnly,
};

enum class SequencerHistoryDirection : uint8_t {
    Undo = 0,
    Redo,
};

enum class SequencerHistoryActionKind : uint8_t {
    PatternEdit = 0,
    StepToggle,
    StepPropertyEdit,
    StepEdit,
    QuickControls,
    PatternSettings,
    PatternVariation,
    ProjectScaleSettings,
    PageStructure,
    TrackStructure,
    CcLaneCreate,
    CcLaneEventEdit,
    CcLaneEventClear,
    CcLaneSettings,
    CcLaneRemove,
    CcLaneTransitionEdit,
    FullBank,
    // Appended so persisted/diagnostic identities of existing actions remain stable.
    PatternRandomize,
};

struct SequencerHistoryDescriptor {
    static constexpr uint8_t INVALID_INDEX = 0xFF;

    SequencerHistoryActionKind kind = SequencerHistoryActionKind::PatternEdit;
    uint8_t trackIndex = INVALID_INDEX;
    uint8_t laneIndex = INVALID_INDEX;
    uint8_t stepIndex = INVALID_INDEX;
    StepProperty property = StepProperty::NOTE;
    bool hasValue = false;
    int32_t beforeValue = 0;
    int32_t afterValue = 0;
};

struct SequencerHistoryApplyResult {
    bool applied = false;
    SequencerHistoryDirection direction = SequencerHistoryDirection::Undo;
    SequencerHistoryDescriptor descriptor{};
};

struct SequencerHistoryPatternChange {
    uint8_t trackIndex = 0;
    SequencerHistoryPatternStorage storage = SequencerHistoryPatternStorage::FullGraph;
    SequencerHistoryDescriptor descriptor{};
    // Deferred runtime activation owned by this exact history operation. Pattern
    // snapshots do not carry canonical Project Track audibility, so the
    // unchanged target mask is retained explicitly for safe Undo/Redo boundary
    // planning (including exclusive Solo selection).
    SequencerTrackActivationHistoryRef activation{};
    uint16_t activationTargetAudibleMask = 0;
    SequencerHistoryPatternSnapshot before;
    SequencerHistoryPatternSnapshot after;

    SequencerHistoryPatternChange();
    ~SequencerHistoryPatternChange();
    SequencerHistoryPatternChange(const SequencerHistoryPatternChange&) = delete;
    SequencerHistoryPatternChange& operator=(const SequencerHistoryPatternChange&) = delete;
    SequencerHistoryPatternChange(SequencerHistoryPatternChange&&) noexcept;
    SequencerHistoryPatternChange& operator=(SequencerHistoryPatternChange&&) noexcept;
};

using SequencerHistoryPatternChangePtr =
    core::app::ExtmemUniquePtr<SequencerHistoryPatternChange>;

struct SequencerHistoryFullBankChange {
    SequencerHistoryDescriptor descriptor{};
    SequencerHistoryTrackBankSnapshot before;
    SequencerHistoryTrackBankSnapshot after;

    SequencerHistoryFullBankChange();
    ~SequencerHistoryFullBankChange();
    SequencerHistoryFullBankChange(const SequencerHistoryFullBankChange&) = delete;
    SequencerHistoryFullBankChange& operator=(const SequencerHistoryFullBankChange&) = delete;
    SequencerHistoryFullBankChange(SequencerHistoryFullBankChange&&) noexcept;
    SequencerHistoryFullBankChange& operator=(SequencerHistoryFullBankChange&&) noexcept;
};

using SequencerHistoryFullBankChangePtr =
    core::app::ExtmemUniquePtr<SequencerHistoryFullBankChange>;
using SequencerHistoryTrackStructureChangePtr =
    core::app::ExtmemUniquePtr<SequencerHistoryTrackStructureChange>;

struct SequencerHistoryEntry {
    SequencerHistoryScope scope = SequencerHistoryScope::PatternOnly;
    core::app::ExtmemUniquePtr<SequencerHistoryPatternChange> pattern;
    SequencerHistoryTrackStructureChangePtr structure;
    core::app::ExtmemUniquePtr<SequencerHistoryFullBankChange> fullBank;

    SequencerHistoryEntry();
    ~SequencerHistoryEntry();
    SequencerHistoryEntry(const SequencerHistoryEntry&) = delete;
    SequencerHistoryEntry& operator=(const SequencerHistoryEntry&) = delete;
    SequencerHistoryEntry(SequencerHistoryEntry&&) noexcept;
    SequencerHistoryEntry& operator=(SequencerHistoryEntry&&) noexcept;

    bool valid() const {
        return (scope == SequencerHistoryScope::PatternOnly && pattern.get() != nullptr) ||
               (scope == SequencerHistoryScope::Structure && structure.get() != nullptr) ||
               (scope == SequencerHistoryScope::FullBank && fullBank.get() != nullptr);
    }
};

bool captureHistorySnapshot(
    const SequencerState& source,
    SequencerHistoryPatternSnapshot& out
);
bool reserveHistorySnapshotGraphStorage(SequencerHistoryPatternSnapshot& snapshot);
bool captureHistorySnapshotUsingReservedGraph(
    const SequencerState& source,
    SequencerHistoryPatternSnapshot& out
);

void captureFlatHistorySnapshot(
    const SequencerState& source,
    SequencerHistoryPatternSnapshot& out
);

bool captureHistorySnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot& out
);
bool captureHistorySnapshotUsingReservedGraph(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot& out
);

void captureFlatHistorySnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint8_t trackIndex,
    SequencerHistoryPatternSnapshot& out
);

bool captureHistorySnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerHistoryTrackBankSnapshot& out
);

bool applyHistorySnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryPatternSnapshot& snapshot
);
bool applyHistorySnapshotToEditor(
    SequencerState& active,
    const SequencerHistoryPatternSnapshot& snapshot
);

bool applyHistorySnapshotToTrack(
    SequencerTrackBankState& bank,
    SequencerState& active,
    uint8_t trackIndex,
    const SequencerHistoryPatternSnapshot& snapshot
);

bool applyHistorySnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryTrackBankSnapshot& snapshot
);

bool sameMusicalHistorySnapshot(
    const SequencerHistoryPatternSnapshot& lhs,
    const SequencerHistoryPatternSnapshot& rhs
);

bool sameMusicalHistorySnapshot(
    const SequencerHistoryTrackBankSnapshot& lhs,
    const SequencerHistoryTrackBankSnapshot& rhs
);

class SequencerHistoryService {
public:
    static constexpr uint8_t PATTERN_ENTRY_LIMIT = 32;
    static constexpr uint8_t STRUCTURE_ENTRY_LIMIT = 8;
    static constexpr uint8_t FULL_BANK_ENTRY_LIMIT = 4;
    static constexpr uint8_t ENTRY_LIMIT =
        PATTERN_ENTRY_LIMIT + STRUCTURE_ENTRY_LIMIT + FULL_BANK_ENTRY_LIMIT;
    static constexpr size_t RETAINED_BYTE_BUDGET = 1024U * 1024U;

    SequencerHistoryService();
    ~SequencerHistoryService();

    void setProjectHistoryEventSink(
        const core::state::project::ProjectHistoryEventSink* sink
    ) {
        project_history_sink_ = sink;
    }

    bool recordPattern(
        uint8_t trackIndex,
        SequencerHistoryPatternSnapshot before,
        SequencerHistoryPatternSnapshot after,
        SequencerHistoryDescriptor descriptor = {}
    );
    bool recordPattern(SequencerHistoryPatternChangePtr change);
    // Side-effect-free admission check for a fully prepared Pattern change.
    bool canRecordPattern(const SequencerHistoryPatternChange& change) const;
    // Precondition: canRecordPattern(change) was true and change was not
    // modified afterwards. Under that contract this commit cannot fail.
    void recordPreparedPattern(SequencerHistoryPatternChangePtr change);

    bool recordFlatPattern(
        uint8_t trackIndex,
        SequencerHistoryPatternSnapshot before,
        SequencerHistoryPatternSnapshot after,
        SequencerHistoryDescriptor descriptor = {}
    );

    bool recordPattern(
        SequencerHistoryPatternSnapshot before,
        SequencerHistoryPatternSnapshot after,
        SequencerHistoryDescriptor descriptor = {}
    );

    bool recordFlatPattern(
        SequencerHistoryPatternSnapshot before,
        SequencerHistoryPatternSnapshot after,
        SequencerHistoryDescriptor descriptor = {}
    );

    bool recordFullBank(
        SequencerHistoryTrackBankSnapshot before,
        SequencerHistoryTrackBankSnapshot after,
        SequencerHistoryDescriptor descriptor = {}
    );
    bool recordFullBank(SequencerHistoryFullBankChangePtr change);
    // Side-effect-free admission check for a fully prepared change. Callers
    // must repeat it if snapshot graph ownership changes before recording.
    bool canRecordStructure(const SequencerHistoryTrackStructureChange& change) const;
    // Precondition: canRecordStructure(change) was true and change was not
    // modified afterwards. Under that contract this commit cannot fail.
    void recordPreparedStructure(SequencerHistoryTrackStructureChangePtr change);
    bool recordStructure(SequencerHistoryTrackStructureChangePtr change);

    bool canUndo() const { return undo_count_ > 0; }
    bool canRedo() const { return redo_count_ > 0; }

    bool undo(SequencerTrackBankState& bank, SequencerState& active);
    bool redo(SequencerTrackBankState& bank, SequencerState& active);
    SequencerHistoryApplyResult undoWithResult(SequencerTrackBankState& bank,
                                               SequencerState& active);
    SequencerHistoryApplyResult redoWithResult(SequencerTrackBankState& bank,
                                               SequencerState& active);
    bool peekUndoTrackActivation(SequencerTrackActivationHistoryPlan& out) const;
    bool peekRedoTrackActivation(SequencerTrackActivationHistoryPlan& out) const;
    const SequencerHistoryMacroTrackStructurePayload*
        peekUndoMacroTrackStructure() const;
    const SequencerHistoryMacroTrackStructurePayload*
        peekRedoMacroTrackStructure() const;

    void clear();
    void discardRedoBranch();

    uint8_t undoCount() const { return undo_count_; }
    uint8_t redoCount() const { return redo_count_; }
    uintptr_t projectHistoryUndoIdentity() const;
    uintptr_t projectHistoryRedoIdentity() const;
    uint8_t undoCount(SequencerHistoryScope scope) const;
    uint8_t redoCount(SequencerHistoryScope scope) const;
    size_t retainedBytes() const;

private:
    std::array<SequencerHistoryEntry, ENTRY_LIMIT> undo_{};
    std::array<SequencerHistoryEntry, ENTRY_LIMIT> redo_{};
    uint8_t undo_count_ = 0;
    uint8_t redo_count_ = 0;
    const core::state::project::ProjectHistoryEventSink* project_history_sink_ = nullptr;

    bool pushUndo(SequencerHistoryEntry entry);
    bool pushRedo(SequencerHistoryEntry entry);
    void commitPreparedEntry(SequencerHistoryEntry entry);
    bool recordEntry(SequencerHistoryEntry entry);
    bool recordPatternWithStorage(
        uint8_t trackIndex,
        SequencerHistoryPatternSnapshot before,
        SequencerHistoryPatternSnapshot after,
        SequencerHistoryDescriptor descriptor,
        SequencerHistoryPatternStorage storage
    );
};

}  // namespace core::state::sequencer
