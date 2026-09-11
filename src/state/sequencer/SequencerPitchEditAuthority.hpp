#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/StepProperty.hpp"

namespace core::state::sequencer::pitch_edit {

// Pure pitch-edit authority for input, content editing and randomization.
// Does not depend on SequencerState or UI state.
bool usesScaleDegreePitchEdit(
    StepProperty property,
    SequencerPitchEditMode mode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
int countScaleNotes(oc::note::sequencer::StepSequencerScaleSettings scaleSettings);
int scaleDegreeIndexForNote(
    uint8_t note,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
uint8_t scaleNoteForDegreeIndex(
    int index,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

}  // namespace core::state::sequencer::pitch_edit
