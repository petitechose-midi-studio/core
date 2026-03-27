#include "ui/sequencer/StepGridFrameLogic.hpp"

namespace core::ui::sequencer::grid {

StepGridFrameState buildStepGridFrameState(const core::state::CoreState& coreState) {
    StepGridFrameState frame;

    const auto& sequencer = coreState.sequencer;
    frame.activeProperty = sequencer.activeStepProperty.get();
    frame.feedbackVisible = sequencer.stepInlineFeedback.visible.get();
    frame.feedbackStep = sequencer.stepInlineFeedback.stepIndex.get();
    frame.feedbackProperty = sequencer.stepInlineFeedback.property.get();

    const uint8_t length = sequencer.length.get();
    const uint8_t page = sequencer.normalizePage(sequencer.page.get());
    const uint8_t pageStart = sequencer.pageStartStep(page);
    const uint64_t enabledMask = sequencer.enabledMask.get();
    const uint64_t probabilityCycleMask = sequencer.probabilityCycleMask;
    const int16_t playhead = sequencer.playheadStep.get();

    for (uint8_t i = 0; i < frame.tiles.size(); ++i) {
        const uint8_t absoluteStep = static_cast<uint8_t>(pageStart + i);
        auto& tile = frame.tiles[i];
        tile.absoluteStep = absoluteStep;
        tile.inPattern = absoluteStep < length;
        tile.enabled = tile.inPattern ? ((enabledMask & (1ULL << absoluteStep)) != 0) : false;
        tile.playing =
            tile.inPattern && (playhead >= 0) && (absoluteStep == static_cast<uint8_t>(playhead));

        if (!tile.inPattern) {
            continue;
        }

        tile.probabilityCycleActive = (probabilityCycleMask & (1ULL << absoluteStep)) != 0;
        tile.note = sequencer.note[absoluteStep];
        tile.velocity = sequencer.velocity[absoluteStep];
        tile.probability = sequencer.probability[absoluteStep];
        tile.gate = sequencer.gate[absoluteStep];
        tile.nudge = sequencer.nudge[absoluteStep];
    }

    return frame;
}

}  // namespace core::ui::sequencer::grid
