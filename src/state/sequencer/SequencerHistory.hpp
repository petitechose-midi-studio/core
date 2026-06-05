#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

using SequencerHistoryGraphPtr =
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>;

struct SequencerHistoryPatternSnapshot {
    SequencerPatternSnapshot flat{};
    uint8_t focusedStep = 0;
    uint8_t page = 0;
    SequencerHistoryGraphPtr graph;

    SequencerHistoryPatternSnapshot() = default;
    SequencerHistoryPatternSnapshot(const SequencerHistoryPatternSnapshot&) = delete;
    SequencerHistoryPatternSnapshot& operator=(const SequencerHistoryPatternSnapshot&) = delete;
    SequencerHistoryPatternSnapshot(SequencerHistoryPatternSnapshot&&) noexcept = default;
    SequencerHistoryPatternSnapshot& operator=(SequencerHistoryPatternSnapshot&&) noexcept = default;
};

struct SequencerHistoryTrackBankSnapshot {
    SequencerTrackBankSnapshot flat{};
    uint8_t focusedStep = 0;
    uint8_t page = 0;
    SequencerHistoryGraphPtr editorGraph;
    std::array<SequencerHistoryGraphPtr, SequencerTrackBankState::TRACK_COUNT> bankGraphs{};

    SequencerHistoryTrackBankSnapshot() = default;
    SequencerHistoryTrackBankSnapshot(const SequencerHistoryTrackBankSnapshot&) = delete;
    SequencerHistoryTrackBankSnapshot& operator=(const SequencerHistoryTrackBankSnapshot&) = delete;
    SequencerHistoryTrackBankSnapshot(SequencerHistoryTrackBankSnapshot&&) noexcept = default;
    SequencerHistoryTrackBankSnapshot& operator=(SequencerHistoryTrackBankSnapshot&&) noexcept = default;
};

enum class SequencerHistoryScope : uint8_t {
    PatternOnly = 0,
    FullBank,
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
    FullBank,
};

struct SequencerHistoryDescriptor {
    static constexpr uint8_t INVALID_INDEX = 0xFF;

    SequencerHistoryActionKind kind = SequencerHistoryActionKind::PatternEdit;
    uint8_t trackIndex = INVALID_INDEX;
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
    SequencerHistoryDescriptor descriptor{};
    SequencerHistoryPatternSnapshot before;
    SequencerHistoryPatternSnapshot after;

    SequencerHistoryPatternChange() = default;
    SequencerHistoryPatternChange(const SequencerHistoryPatternChange&) = delete;
    SequencerHistoryPatternChange& operator=(const SequencerHistoryPatternChange&) = delete;
    SequencerHistoryPatternChange(SequencerHistoryPatternChange&&) noexcept = default;
    SequencerHistoryPatternChange& operator=(SequencerHistoryPatternChange&&) noexcept = default;
};

struct SequencerHistoryFullBankChange {
    SequencerHistoryDescriptor descriptor{};
    SequencerHistoryTrackBankSnapshot before;
    SequencerHistoryTrackBankSnapshot after;

    SequencerHistoryFullBankChange() = default;
    SequencerHistoryFullBankChange(const SequencerHistoryFullBankChange&) = delete;
    SequencerHistoryFullBankChange& operator=(const SequencerHistoryFullBankChange&) = delete;
    SequencerHistoryFullBankChange(SequencerHistoryFullBankChange&&) noexcept = default;
    SequencerHistoryFullBankChange& operator=(SequencerHistoryFullBankChange&&) noexcept = default;
};

using SequencerHistoryFullBankChangePtr =
    core::app::ExtmemUniquePtr<SequencerHistoryFullBankChange>;

struct SequencerHistoryEntry {
    SequencerHistoryScope scope = SequencerHistoryScope::PatternOnly;
    core::app::ExtmemUniquePtr<SequencerHistoryPatternChange> pattern;
    core::app::ExtmemUniquePtr<SequencerHistoryFullBankChange> fullBank;

    SequencerHistoryEntry() = default;
    SequencerHistoryEntry(const SequencerHistoryEntry&) = delete;
    SequencerHistoryEntry& operator=(const SequencerHistoryEntry&) = delete;
    SequencerHistoryEntry(SequencerHistoryEntry&&) noexcept = default;
    SequencerHistoryEntry& operator=(SequencerHistoryEntry&&) noexcept = default;

    bool valid() const {
        return (scope == SequencerHistoryScope::PatternOnly && pattern.get() != nullptr) ||
               (scope == SequencerHistoryScope::FullBank && fullBank.get() != nullptr);
    }
};

bool captureHistorySnapshot(
    const SequencerState& source,
    SequencerHistoryPatternSnapshot& out
);

bool captureHistorySnapshot(
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
    static constexpr uint8_t FULL_BANK_ENTRY_LIMIT = 4;
    static constexpr uint8_t ENTRY_LIMIT = PATTERN_ENTRY_LIMIT + FULL_BANK_ENTRY_LIMIT;

    bool recordPattern(
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

    bool recordFullBank(
        SequencerHistoryTrackBankSnapshot before,
        SequencerHistoryTrackBankSnapshot after,
        SequencerHistoryDescriptor descriptor = {}
    );
    bool recordFullBank(SequencerHistoryFullBankChangePtr change);

    bool canUndo() const { return undo_count_ > 0; }
    bool canRedo() const { return redo_count_ > 0; }

    bool undo(SequencerTrackBankState& bank, SequencerState& active);
    bool redo(SequencerTrackBankState& bank, SequencerState& active);
    SequencerHistoryApplyResult undoWithResult(SequencerTrackBankState& bank,
                                               SequencerState& active);
    SequencerHistoryApplyResult redoWithResult(SequencerTrackBankState& bank,
                                               SequencerState& active);

    void clear();

    uint8_t undoCount() const { return undo_count_; }
    uint8_t redoCount() const { return redo_count_; }
    uint8_t undoCount(SequencerHistoryScope scope) const;
    uint8_t redoCount(SequencerHistoryScope scope) const;

private:
    std::array<SequencerHistoryEntry, ENTRY_LIMIT> undo_{};
    std::array<SequencerHistoryEntry, ENTRY_LIMIT> redo_{};
    uint8_t undo_count_ = 0;
    uint8_t redo_count_ = 0;

    bool pushUndo(SequencerHistoryEntry entry);
    bool pushRedo(SequencerHistoryEntry entry);
};

}  // namespace core::state::sequencer
