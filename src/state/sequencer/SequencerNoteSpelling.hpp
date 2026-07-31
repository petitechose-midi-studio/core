#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

namespace core::state::sequencer::note_spelling {

const char* pitchClassLabel(
    uint8_t pitchClass,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

void formatNoteName(
    char* out,
    size_t outSize,
    uint8_t note,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

}  // namespace core::state::sequencer::note_spelling
