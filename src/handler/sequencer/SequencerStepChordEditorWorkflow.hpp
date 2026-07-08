#pragma once

#include <cstdint>

#include <oc/api/EncoderAPI.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>
#include <oc/type/Ids.hpp>

#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::step_chord_editor_workflow {

bool active(const core::state::sequencer::SequencerState& sequencer);
void open(core::state::sequencer::SequencerState& sequencer);
void close(core::state::sequencer::SequencerState& sequencer);
void moveFocus(core::state::sequencer::SequencerState& sequencer, float delta);
void setFocusedFieldValue(core::state::sequencer::SequencerState& sequencer,
                          uint8_t step,
                          oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
                          float normalized);
void configureFocusedFieldEncoder(oc::api::EncoderAPI& encoders,
                                  oc::type::EncoderID encoderId,
                                  core::state::sequencer::SequencerState& sequencer,
                                  uint8_t step,
                                  oc::note::sequencer::StepSequencerScaleSettings scaleSettings);
bool resetFocusedFieldToDefault(core::state::sequencer::SequencerState& sequencer,
                                uint8_t step);

}  // namespace core::handler::sequencer::step_chord_editor_workflow
