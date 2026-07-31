#pragma once

#include <cstdint>

#include <oc/api/EncoderAPI.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>

namespace core::state::sequencer {
struct SequencerState;
}

namespace core::handler::sequencer::step_chord_editor_workflow {

bool active(const core::state::sequencer::SequencerState& sequencer);
bool formulaEditorActive(
    const core::state::sequencer::SequencerState& sequencer
);
bool sourceSelectorActive(
    const core::state::sequencer::SequencerState& sequencer
);
void open(core::state::sequencer::SequencerState& sequencer);
void close(core::state::sequencer::SequencerState& sequencer);
bool cancelSubEditor(core::state::sequencer::SequencerState& sequencer);
void toggleSourceSelector(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
bool activateFocusedItem(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
void moveFocus(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float delta
);
void setFocusedFieldValue(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float normalized
);
void configureFocusedFieldEncoder(
    oc::api::EncoderAPI& encoders,
    oc::type::EncoderID encoderId,
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
bool resetFocusedFieldToDefault(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

}  // namespace core::handler::sequencer::step_chord_editor_workflow
