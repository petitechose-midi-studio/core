#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

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
    core::state::sequencer::StepProperty displayProperty =
        core::state::sequencer::StepProperty::NOTE;
};

InlineFeedbackSnapshot readInlineFeedbackSnapshot(
    bool visible,
    oc::note::sequencer::StepBitMask128 touchedMask,
    core::state::sequencer::StepProperty property
);

NoteLabelPresentation buildNoteLabelPresentation(
    const TileRenderState& state,
    const StepGridFrameState& frameState
);

}  // namespace core::ui::sequencer::grid
