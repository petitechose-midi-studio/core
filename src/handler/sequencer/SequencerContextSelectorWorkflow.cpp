#include "handler/sequencer/SequencerContextSelectorWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include <oc/time/Time.hpp>

namespace core::handler {

using Focus = core::state::StructureNavigationFocus;
using Feedback = core::state::sequencer::SequencerContextSelectorFeedback;

FLASHMEM SequencerContextSelectorWorkflow::SequencerContextSelectorWorkflow(
    core::state::sequencer::SequencerContextSelectorState& state
) : state_(state) {}

FLASHMEM void SequencerContextSelectorWorkflow::press(
    Focus current,
    bool includeTrack
) {
    gesture_.press();
    include_track_ = includeTrack;
    state_.feedbackUntilMs = 0;
    state_.feedback = Feedback::NONE;
    state_.previewFocus = !includeTrack && current == Focus::TRACK
        ? Focus::PAGE
        : current;
    state_.visible = true;
    state_.bump();
}

FLASHMEM bool SequencerContextSelectorWorkflow::holdForSelection() {
    if (!gesture_.active() || gesture_.turned()) return false;
    gesture_.hold();
    gesture_.cancel();
    state_.visible = false;
    state_.feedback = Feedback::NONE;
    state_.feedbackUntilMs = 0;
    state_.bump();
    return true;
}

FLASHMEM bool SequencerContextSelectorWorkflow::turn(float delta) {
    if (!gesture_.turn(delta != 0.0f)) return false;
    const int direction = delta > 0.0f ? 1 : -1;
    state_.previewFocus = adjacent(
        state_.previewFocus,
        direction,
        include_track_
    );
    state_.bump();
    return true;
}

FLASHMEM SequencerContextSelectorOutcome SequencerContextSelectorWorkflow::release(
    uint32_t nowMs
) {
    if (!gesture_.active()) return {};

    const Focus selected = state_.previewFocus;
    const auto release = gesture_.release();

    if (release == PressHoldTurnReleaseGesture::Release::TURN) {
        state_.visible = false;
        state_.feedback = Feedback::NONE;
        state_.bump();
        return {SequencerContextSelectorAction::APPLY_CONTEXT, selected};
    }
    if (selected == Focus::STEP) {
        state_.visible = false;
        state_.feedback = Feedback::NONE;
        state_.bump();
        return {SequencerContextSelectorAction::OPEN_STEP_EDITOR, selected};
    }
    if (selected == Focus::PAGE) {
        state_.visible = false;
        state_.feedback = Feedback::NONE;
        state_.bump();
        return {SequencerContextSelectorAction::OPEN_PATTERN_EDITOR, selected};
    }

    (void)nowMs;
    state_.feedbackUntilMs = 0U;
    state_.feedback = Feedback::NONE;
    state_.visible = false;
    state_.bump();
    return {SequencerContextSelectorAction::OPEN_TRACK_EDITOR, selected};
}

FLASHMEM void SequencerContextSelectorWorkflow::update(uint32_t nowMs) {
    if (gesture_.active() && !state_.visible) {
        gesture_.cancel();
        return;
    }
    if (gesture_.active() || state_.feedback == Feedback::NONE ||
        !state_.visible) {
        return;
    }
    if (!oc::time::deadlineReachedMs(nowMs, state_.feedbackUntilMs)) return;
    state_.visible = false;
    state_.feedback = Feedback::NONE;
    state_.feedbackUntilMs = 0;
    state_.bump();
}

FLASHMEM void SequencerContextSelectorWorkflow::cancel() {
    gesture_.cancel();
    state_.reset();
}

FLASHMEM Focus SequencerContextSelectorWorkflow::adjacent(
    Focus current,
    int direction,
    bool includeTrack
) {
    if (!includeTrack) {
        return current == Focus::STEP ? Focus::PAGE : Focus::STEP;
    }
    // Musical order is Track -> Pattern(PAGE) -> Step, with wrap.
    constexpr Focus order[] = {Focus::TRACK, Focus::PAGE, Focus::STEP};
    int index = 1;
    for (int i = 0; i < 3; ++i) {
        if (order[i] == current) {
            index = i;
            break;
        }
    }
    const int next = (index + (direction > 0 ? 1 : 2)) % 3;
    return order[next];
}

}  // namespace core::handler
