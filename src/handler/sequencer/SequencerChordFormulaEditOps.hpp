#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerChordUiOps.hpp"

namespace core::state::sequencer {
struct SequencerState;
}

namespace core::handler::sequencer::chord_edit_ops {

bool applyFormulaVoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float normalized
);

bool addFormulaVoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

bool applyFormulaVoiceRemoveIntent(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

uint8_t formulaVoiceCount(
    const core::state::sequencer::SequencerStepChordUiState& chord
);
bool formulaAddAvailable(
    const core::state::sequencer::SequencerStepChordUiState& chord
);
float formulaVoiceToNormalized(
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex
);
uint8_t formulaVoiceChoiceCount(
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex
);

}  // namespace core::handler::sequencer::chord_edit_ops
