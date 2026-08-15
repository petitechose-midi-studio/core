#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::ui::sequencer::grid {

/**
 * Pure label presentation rules for step-grid inline feedback.
 *
 * Logic here decides which property label/icon should appear for a tile; actual
 * label text, positioning, and LVGL style updates happen in the renderer.
 */
struct InlineFeedbackSnapshot {
    bool visible = false;
    oc::note::sequencer::StepBitMask128 touchedMask{};
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
    oc::note::sequencer::StepBitMask128 touchedMask,
    core::state::sequencer::StepProperty property
);

NoteLabelPresentation buildNoteLabelPresentation(
    const TileRenderState& state,
    const visual::StepPropertyVisualSpec& propertyVisual,
    core::state::sequencer::StepProperty activeProperty,
    const InlineFeedbackSnapshot& feedback,
    StepGridPresentation presentation = StepGridPresentation::MELODIC
);

}  // namespace core::ui::sequencer::grid
