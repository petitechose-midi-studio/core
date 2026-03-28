#pragma once

#include <cstdint>

#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::ui::sequencer::grid {

struct InlineFeedbackSnapshot {
    bool visible = false;
    uint64_t touchedMask = 0;
    core::state::sequencer::StepProperty property =
        core::state::sequencer::StepProperty::NOTE;
};

struct NoteLabelPresentation {
    bool showLabel = false;
    bool showInlineIcon = false;
    bool probabilityMasked = false;
    bool showNoteStyle = false;
    core::state::sequencer::StepProperty displayProperty =
        core::state::sequencer::StepProperty::NOTE;
};

core::state::sequencer::StepProperty displayPropertyForInlineLabelMode(
    visual::InlineLabelMode mode
);

InlineFeedbackSnapshot readInlineFeedbackSnapshot(
    bool visible,
    uint64_t touchedMask,
    core::state::sequencer::StepProperty property
);

NoteLabelPresentation buildNoteLabelPresentation(
    const TileRenderState& state,
    const visual::StepPropertyVisualSpec& propertyVisual,
    core::state::sequencer::StepProperty activeProperty,
    const InlineFeedbackSnapshot& feedback
);

}  // namespace core::ui::sequencer::grid
