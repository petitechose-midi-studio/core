#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

namespace core::state::sequencer {

struct SequencerState;

/**
 * Cold authoring projection of the runtime root-step expansion budget.
 *
 * requestedNoteCount saturates at 17, which means "more than the retained
 * 16-note engine budget". No expanded-note array is retained by this view.
 */
struct SequencerExpansionBudgetProjection {
    bool valid = false;
    uint8_t rootStepIndex = 0;
    uint8_t emittedNoteCount = 0;
    uint8_t requestedNoteCount = 0;
    bool noteBudgetExceeded = false;
    bool depthLimitReached = false;
};

SequencerExpansionBudgetProjection projectSequencerExpansionBudget(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    uint8_t activeContentStep
);

}  // namespace core::state::sequencer
