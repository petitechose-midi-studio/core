#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::state::sequencer {
struct SequencerState;
}

namespace core::handler::sequencer::chord_edit_ops {

int editFieldCount();
int modeChoiceCount(bool rootContext);
int modeChoiceIndex(
    bool rootContext,
    oc::note::sequencer::StepSequencerChordMode mode
);
core::state::sequencer::SequencerChordSourceChoice sourceChoiceForMode(
    bool rootContext,
    oc::note::sequencer::StepSequencerChordMode mode
);
int sourceChoiceCount(bool rootContext);
int sourceChoiceIndex(
    bool rootContext,
    core::state::sequencer::SequencerChordSourceChoice choice
);
core::state::sequencer::SequencerChordSourceChoice sourceChoiceForIndex(
    bool rootContext,
    int index
);

int quickChoiceCount(bool rootContext);
int quickChoiceIndex(
    const core::state::sequencer::SequencerStepChordUiState& chord
);
void applyQuickChoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    int choice,
    bool scaleBased
);

bool applyModeChoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    int choice,
    oc::note::sequencer::StepSequencerChordSpec specForLocal,
    bool scaleBased
);
bool applySourceChoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordSourceChoice choice,
    const core::state::sequencer::SequencerStepChordUiState& chord
);
bool createDefaultLocalChord(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    bool scaleBased
);
bool captureAuthoringSnapshot(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordAuthoringSnapshot& snapshot
);
bool restoreAuthoringSnapshot(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerChordAuthoringSnapshot& snapshot
);

bool applySpecField(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordEditField field,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float normalized
);

bool resetSpecField(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordEditField field,
    const core::state::sequencer::SequencerStepChordUiState& chord
);

float signedToNormalized(int value, int minValue, int maxValue);

}  // namespace core::handler::sequencer::chord_edit_ops
