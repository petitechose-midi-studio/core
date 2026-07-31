#pragma once

#include <cstdint>

#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::state::sequencer {

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

bool appendPage(SequencerState& target);
bool insertPage(SequencerState& target, uint8_t pageIndex);
bool ensurePageExists(SequencerState& target, uint8_t pageIndex);
bool deletePage(SequencerState& target, uint8_t pageIndex);

}  // namespace core::state::sequencer
