#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <cstdio>

#include "ui/sequencer/StepGridFrameLogic.hpp"

namespace core::ui::sequencer {

SequencerHeaderBarProps buildHeaderBarProps(const core::state::CoreState& coreState) {
    const auto& sequencer = coreState.sequencer;

    static char leftText[24];
    static char rightText[24];

    std::snprintf(leftText, sizeof(leftText), "Track 1");
    std::snprintf(
        rightText,
        sizeof(rightText),
        "%u steps",
        static_cast<unsigned>(sequencer.length.get())
    );

    return {
        .length = sequencer.length.get(),
        .viewedPage = sequencer.normalizePage(sequencer.page.get()),
        .playheadStep = sequencer.playheadStep.get(),
        .leftText = leftText,
        .centerText = "",
        .rightText = rightText,
        .dimmed = false,
    };
}

PatternQuickControlsProps buildPatternQuickControlsProps(const core::state::CoreState& coreState) {
    const auto& sequencer = coreState.sequencer;

    return {
        .selecting = sequencer.patternQuickControls.selecting.get(),
        .focusedItem = sequencer.patternQuickControls.focusedItem.get(),
        .midiChannel = sequencer.midiChannel.get(),
        .stepsPerBeat = sequencer.stepsPerBeat.get(),
        .length = sequencer.length.get(),
    };
}

StepPropertyStripProps buildStepPropertyStripProps(const core::state::CoreState& coreState) {
    const auto& sequencer = coreState.sequencer;

    return {
        .activeProperty = sequencer.activeStepProperty.get(),
        .selecting = sequencer.stepPropertyInlineSelector.selecting.get(),
        .selectedIndex = sequencer.stepPropertyInlineSelector.selectedIndex.get(),
    };
}

grid::StepGridFrameState buildStepGridProps(const core::state::CoreState& coreState) {
    return grid::buildStepGridFrameState(coreState);
}

}  // namespace core::ui::sequencer
