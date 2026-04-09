#include "ui/sequencer/StepGridFrameLogic.hpp"

#include <algorithm>

namespace core::ui::sequencer::grid {

StepGridFrameState buildStepGridFrameState(const core::state::sequencer::SequencerState& sequencer) {
    StepGridFrameState frame;

    frame.activeProperty = sequencer.activeStepProperty.get();
    frame.feedbackVisible = sequencer.stepInlineFeedback.visible.get();
    frame.feedbackTouchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    frame.feedbackProperty = sequencer.stepInlineFeedback.property.get();
    frame.selection.active = sequencer.rangeSelection.active();
    frame.selection.kind = sequencer.rangeSelection.kind.get();
    frame.selection.phase = sequencer.rangeSelection.phase.get();
    frame.selection.cursorStep = sequencer.rangeSelection.cursorStep.get();

    if (frame.selection.active) {
        switch (frame.selection.phase) {
            case core::state::sequencer::RangeSelectionPhase::SELECT_RANGE:
            case core::state::sequencer::RangeSelectionPhase::PASTE_TARGET:
                frame.selection.sourceRangeVisible = true;
                frame.selection.sourceStart = sequencer.rangeSelection.rangeStart.get();
                frame.selection.sourceEnd = sequencer.rangeSelection.rangeEnd.get();
                break;
            case core::state::sequencer::RangeSelectionPhase::IDLE:
            default:
                break;
        }
    }

    const uint8_t length = sequencer.length.get();
    const uint8_t page = sequencer.visiblePage();
    const uint8_t pageStart = sequencer.pageStartStepClamped(page);
    const auto enabledMask = sequencer.enabledMask.get();
    const auto probabilityCycleMask = sequencer.probabilityCycleMask;
    const int16_t playhead = sequencer.playheadStep.get();

    for (uint8_t i = 0; i < frame.tiles.size(); ++i) {
        const uint8_t absoluteStep = static_cast<uint8_t>(pageStart + i);
        auto& tile = frame.tiles[i];
        tile.absoluteStep = absoluteStep;
        tile.inPattern = absoluteStep < length;
        tile.enabled = tile.inPattern ? enabledMask.test(absoluteStep) : false;
        tile.playing =
            tile.inPattern && (playhead >= 0) && (absoluteStep == static_cast<uint8_t>(playhead));

        if (!tile.inPattern) {
            continue;
        }

        tile.probabilityCycleActive = probabilityCycleMask.test(absoluteStep);
        tile.note = sequencer.note[absoluteStep];
        tile.velocity = sequencer.velocity[absoluteStep];
        tile.probability = sequencer.probability[absoluteStep];
        tile.gate = sequencer.gate[absoluteStep];
        tile.nudge = sequencer.nudge[absoluteStep];
    }

    return frame;
}

}  // namespace core::ui::sequencer::grid
