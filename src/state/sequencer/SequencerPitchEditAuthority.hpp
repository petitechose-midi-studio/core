#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/StepProperty.hpp"

namespace core::state::sequencer::content_view_internal {

// Canonical integer pitch-edit helpers shared by every authoring domain that
// needs to express a Note delta in semitones or effective-scale degrees. Their
// implementation remains owned by SequencerContentViewInternal.cpp; this
// lightweight declaration surface avoids importing SequencerState/UI state.
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

}  // namespace core::state::sequencer::content_view_internal
