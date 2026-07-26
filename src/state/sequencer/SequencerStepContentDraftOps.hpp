#pragma once

#include <cstdint>

#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerStepContentDraftSession.hpp"

namespace core::state::sequencer {

struct SequencerState;

/**
 * Captures the graph currently heard while a Step-content draft is active.
 * The caller owns the preallocated destination; this helper never allocates.
 */
[[nodiscard]] bool captureStepContentDraftRuntimeGraph(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerGraph& out
);

[[nodiscard]] SequencerPatternState& authoringPattern(SequencerState& sequencer);
[[nodiscard]] const SequencerPatternState& authoringPattern(
    const SequencerState& sequencer
);

[[nodiscard]] bool beginStepContentDraft(
    SequencerState& sequencer,
    SequencerStepContentDraftKind kind,
    uint8_t ownerStep,
    uint16_t ownerNodeId = SequencerStepChordDraftState::INVALID_NODE
);
void markStepContentDraftPristine(SequencerState& sequencer);
void notifyStepContentDraftMutation(SequencerState& sequencer);
[[nodiscard]] bool stepContentDraftHasPublishableSubset(
    const SequencerState& sequencer
);

[[nodiscard]] bool resolveStepContentDraftChord(
    const SequencerState& sequencer,
    uint16_t nodeId,
    bool& modePresent,
    bool& localPresent,
    oc::note::sequencer::StepSequencerChordMode& mode,
    oc::note::sequencer::StepSequencerChordSpec& spec
);
[[nodiscard]] bool setAuthoringNodeChordMode(
    SequencerState& sequencer,
    uint16_t nodeId,
    oc::note::sequencer::StepSequencerChordMode mode
);
[[nodiscard]] bool setAuthoringNodeChordSpec(
    SequencerState& sequencer,
    uint16_t nodeId,
    oc::note::sequencer::StepSequencerChordSpec spec
);
[[nodiscard]] bool clearAuthoringNodeChordState(
    SequencerState& sequencer,
    uint16_t nodeId
);

/** Fill a preallocated history snapshot with the exact prospective publish. */
[[nodiscard]] bool captureStepContentDraftAfterSnapshot(
    const SequencerState& sequencer,
    SequencerHistoryPatternSnapshot& out
);

/**
 * Publish the prepared scratch graph. Allocation is completed before mutation;
 * failure therefore leaves the published Pattern and active draft untouched.
 */
[[nodiscard]] bool publishStepContentDraft(SequencerState& sequencer);

void abandonStepContentDraft(SequencerState& sequencer);

}  // namespace core::state::sequencer
