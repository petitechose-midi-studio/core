#pragma once

#include <cstdint>

#include <oc/api/EncoderAPI.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>
#include <oc/type/Ids.hpp>

#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::step_value_row_workflow {

bool focusedRowIsValue(const core::state::sequencer::SequencerState& sequencer);
bool focusedRowSupportsLocalVariation(
    const core::state::sequencer::SequencerState& sequencer
);

void setFocusedRowValue(core::state::sequencer::SequencerState& sequencer,
                        uint8_t step,
                        oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
                        float normalized);
void configureFocusedRowEncoder(
    oc::api::EncoderAPI& encoders,
    oc::type::EncoderID encoderId,
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
bool resetFocusedRowToDefault(core::state::sequencer::SequencerState& sequencer,
                              uint8_t step);

}  // namespace core::handler::sequencer::step_value_row_workflow
