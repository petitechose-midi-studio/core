#pragma once

#include <cstdint>

#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::state::sequencer {

enum class SequencerSnapshotBatchMutationStatus : uint8_t {
    APPLIED = 0,
    NO_CHANGE,
    INVALID_ARGUMENT,
    INVALID_PATTERN_STATE,
    INVALID_GRAPH,
    INVALID_CC_LANE_BANK,
};

/** Revision domains dirtied by one unversioned structural batch. */
struct SequencerSnapshotBatchDomains {
    bool stepData = false;
    bool graph = false;
    bool ccLanes = false;
    bool timing = false;

    [[nodiscard]] bool any() const noexcept {
        return stepData || graph || ccLanes || timing;
    }
};

struct SequencerSnapshotBatchMutationResult {
    SequencerSnapshotBatchMutationStatus status =
        SequencerSnapshotBatchMutationStatus::INVALID_ARGUMENT;
    SequencerSnapshotBatchDomains domains{};
    uint8_t previousLength = 0U;
    uint8_t resultingLength = 0U;

    [[nodiscard]] bool accepted() const noexcept {
        return status == SequencerSnapshotBatchMutationStatus::APPLIED ||
               status == SequencerSnapshotBatchMutationStatus::NO_CHANGE;
    }

    [[nodiscard]] bool changed() const noexcept {
        return status == SequencerSnapshotBatchMutationStatus::APPLIED;
    }
};

static_assert(sizeof(SequencerSnapshotBatchDomains) == 4U);
static_assert(sizeof(SequencerSnapshotBatchMutationResult) <= 8U);

/**
 * Pure sequencer snapshot and structural pattern operations.
 *
 * These functions sanitize persisted input, maintain focused step/page
 * consistency, and bump stepDataRevision when step content changes.
 */
oc::note::sequencer::StepBitMask128 lengthMask(uint8_t length);

void captureSnapshot(const SequencerPatternState& source, SequencerPatternSnapshot& out);

void applySnapshot(SequencerPatternState& target, const SequencerPatternSnapshot& snapshot);

// Applies scalar pattern state without replacing the graph allocation.
void applySnapshotPreservingGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot
);

[[nodiscard]] bool copyPatternState(
    SequencerPatternState& target,
    const SequencerPatternState& source
);

[[nodiscard]] bool applySnapshotWithGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
);

/** Applies copied Track musical content and graph. Routing is Project-owned. */
[[nodiscard]] bool applyTrackContentSnapshotWithGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
);

/** Installs already-cloned Track content without allocating. */
void installTrackContentSnapshotWithOwnedGraph(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph
);

// Copies scalar pattern state when graph revisions are already synchronized.
[[nodiscard]] bool copyPatternStatePreservingGraph(
    SequencerPatternState& target,
    const SequencerPatternState& source
);

/** Installs a complete copied Track payload, including Pattern-owned CC lanes. */
void installTrackContentSnapshotWithOwnedPayload(
    SequencerPatternState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph,
    SequencerCcLaneBankPtr ccLanes
);

void applySnapshotToEditor(SequencerState& target, const SequencerPatternSnapshot& snapshot);

void applySnapshotToEditorPreservingGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot
);

[[nodiscard]] bool applySnapshotToEditorWithGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
);

/** Applies copied Track musical content and graph to the active editor. */
[[nodiscard]] bool applyTrackContentSnapshotToEditorWithGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    const oc::note::sequencer::StepSequencerGraph* graph
);

void installTrackContentSnapshotToEditorWithOwnedGraph(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph
);

void installTrackContentSnapshotToEditorWithOwnedPayload(
    SequencerState& target,
    const SequencerPatternSnapshot& snapshot,
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph,
    SequencerCcLaneBankPtr ccLanes
);

// Installs a decoded/staged pattern without allocating another graph copy.
void installPatternStateToEditor(
    SequencerState& target,
    SequencerPatternState& staged
);

// Merges staged flat data and transfers its graph ownership into the editor.
void mergePatternStateIntoCurrent(
    SequencerState& target,
    SequencerPatternState& staged
);

void mergeSnapshotIntoCurrent(SequencerState& target, const SequencerPatternSnapshot& snapshot);

bool duplicatePatternForward(SequencerState& target);

bool rotatePattern(SequencerState& target, int offsetSteps);

bool clearStepRange(SequencerState& target, uint8_t startStep, uint8_t endStep);

/**
 * Extends Content Length to the exact requested length and canonicalizes the
 * new root Steps. Pattern-owned CC events are preserved byte-for-byte.
 * Shrinking is rejected; an equal length is a no-op. Allocation-free,
 * non-compacting and unversioned.
 */
[[nodiscard]] SequencerSnapshotBatchMutationResult
resizeSequencerRootContentUnversioned(
    SequencerState& target,
    uint8_t requiredLength
) noexcept;

/**
 * Extends Content Length through pageIndex and canonicalizes the new root
 * Page. This operation delegates to the exact-length primitive above.
 */
[[nodiscard]] SequencerSnapshotBatchMutationResult
extendSequencerPageRootUnversioned(
    SequencerState& target,
    uint8_t pageIndex
) noexcept;

/**
 * Clears flat, enabled and root-Graph content in [startStep, startStep+count).
 * Pattern-owned CC lanes and playback boundaries are preserved byte-for-byte.
 */
[[nodiscard]] SequencerSnapshotBatchMutationResult
clearSequencerRootStepSpanUnversioned(
    SequencerState& target,
    uint8_t startStep,
    uint8_t stepCount
) noexcept;

/**
 * Atomically removes a sparse mask of existing root Pages in one forward pass.
 * Bits outside activePageCount are rejected and at least one Page must remain.
 * No allocation, Graph compaction or revision publication occurs.
 */
[[nodiscard]] SequencerSnapshotBatchMutationResult
deleteSequencerRootPagesUnversioned(
    SequencerState& target,
    uint16_t pageMask
) noexcept;

/** Publish each dirty content revision at most once after a successful batch. */
void publishSequencerSnapshotBatchRevisions(
    SequencerPatternState& pattern,
    const SequencerSnapshotBatchDomains& domains
) noexcept;

// Native-regression lease only: these versioned insertion wrappers exercise
// region/Graph shifting in SnapshotOps tests. Product handlers must use the
// prepared Page transaction surface; the architecture gate enforces that.
bool appendPage(SequencerState& target);
bool insertPage(SequencerState& target, uint8_t pageIndex);
bool deletePage(SequencerState& target, uint8_t pageIndex);

}  // namespace core::state::sequencer
