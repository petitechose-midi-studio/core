#pragma once

#include <cstdint>

#include "state/sequencer/SequencerPatternState.hpp"
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

void copyPatternState(SequencerPatternState& target, const SequencerPatternState& source);

void applySnapshotToEditor(SequencerState& target, const SequencerPatternSnapshot& snapshot);

void mergeSnapshotIntoCurrent(SequencerState& target, const SequencerPatternSnapshot& snapshot);

bool duplicatePatternForward(SequencerState& target);

bool rotatePattern(SequencerState& target, int offsetSteps);

bool clearStepRange(SequencerState& target, uint8_t startStep, uint8_t endStep);

bool appendPage(SequencerState& target);
bool insertPage(SequencerState& target, uint8_t pageIndex);
bool ensurePageExists(SequencerState& target, uint8_t pageIndex);
bool removePage(SequencerState& target, uint8_t pageIndex);

}  // namespace core::state::sequencer
