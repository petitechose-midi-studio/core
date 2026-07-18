#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerChord.hpp>

#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::state::sequencer {
struct SequencerState;
}  // namespace core::state::sequencer

namespace core::handler::sequencer::chord_edit_ops {

int editFieldCount();

int modeChoiceCount(bool rootContext);
int modeChoiceIndex(bool rootContext, oc::note::sequencer::StepSequencerChordMode mode);

int quickChoiceCount(bool rootContext);
int quickChoiceIndex(const core::state::sequencer::SequencerStepChordUiState& chord);

void applyQuickChoice(core::state::sequencer::SequencerState& sequencer,
                      uint8_t step,
                      int choice,
                      bool scaleConstrained);

bool applyModeChoice(core::state::sequencer::SequencerState& sequencer,
                     uint8_t step,
                     int choice,
                     oc::note::sequencer::StepSequencerChordSpec specForLocal,
                     bool scaleConstrained);

bool applySpecField(core::state::sequencer::SequencerState& sequencer,
                    uint8_t step,
                    core::state::sequencer::SequencerChordEditField field,
                    oc::note::sequencer::StepSequencerChordSpec spec,
                    bool scaleConstrained,
                    float normalized);

bool resetSpecField(core::state::sequencer::SequencerState& sequencer,
                    uint8_t step,
                    core::state::sequencer::SequencerChordEditField field,
                    bool scaleConstrained);

float voiceCountToNormalized(uint8_t voiceCount);
float signedToNormalized(int value, int minValue, int maxValue);

}  // namespace core::handler::sequencer::chord_edit_ops
