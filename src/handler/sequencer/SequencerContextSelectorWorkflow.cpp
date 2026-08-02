#include "handler/sequencer/SequencerContextSelectorWorkflow.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler {

using Focus = core::state::StructureNavigationFocus;

FLASHMEM SequencerContextSelectorWorkflow::SequencerContextSelectorWorkflow(
    core::state::sequencer::SequencerContextSelectorState& state
) : state_(state) {}

FLASHMEM void SequencerContextSelectorWorkflow::press(
    Focus current,
    bool includeTrack,
    uint8_t previewTarget,
    bool previewAddSlot
) {
    gesture_.press();
    state_.previewFocus = !includeTrack && current == Focus::TRACK
        ? Focus::PAGE
        : current;
    press_target_ = previewTarget;
    press_context_ = static_cast<uint8_t>(
        (static_cast<uint8_t>(state_.previewFocus) & 0x03U) |
        (previewAddSlot ? 0x04U : 0U) |
        (includeTrack ? 0x08U : 0U)
    );
    state_.visible = true;
    state_.bump();
}

FLASHMEM bool SequencerContextSelectorWorkflow::holdForSelection(
    Focus current,
    uint8_t previewTarget,
    bool previewAddSlot
) {
    if (!state_.visible) {
        gesture_.cancel();
        press_context_ = 0U;
        press_target_ = 0U;
        return false;
    }
    if (!gesture_.active() || gesture_.turned()) return false;
    const Focus origin = static_cast<Focus>(press_context_ & 0x03U);
    const bool pressMatches = current == origin &&
        previewTarget == press_target_ &&
        previewAddSlot == ((press_context_ & 0x04U) != 0U);
    if (!pressMatches) {
        cancel();
        return false;
    }
    gesture_.cancel();
    press_context_ = 0U;
    press_target_ = 0U;
    state_.visible = false;
    state_.bump();
    return true;
}

FLASHMEM bool SequencerContextSelectorWorkflow::turn(float delta) {
    if (!state_.visible) {
        gesture_.cancel();
        press_context_ = 0U;
        press_target_ = 0U;
        return false;
    }
    if (!gesture_.turn(delta != 0.0f)) return false;
    const int direction = delta > 0.0f ? 1 : -1;
    state_.previewFocus = adjacent(
        state_.previewFocus,
        direction,
        (press_context_ & 0x08U) != 0U
    );
    state_.bump();
    return true;
}

FLASHMEM SequencerContextSelectorOutcome SequencerContextSelectorWorkflow::release() {
    if (!state_.visible) {
        gesture_.cancel();
        press_context_ = 0U;
        press_target_ = 0U;
        return {};
    }
    if (!gesture_.active()) return {};

    const Focus selected = state_.previewFocus;
    const Focus origin = static_cast<Focus>(press_context_ & 0x03U);
    const uint8_t previewTarget = press_target_;
    const bool previewAddSlot = (press_context_ & 0x04U) != 0U;
    press_context_ = 0U;
    press_target_ = 0U;
    const auto release = gesture_.release();

    if (release == PressHoldTurnReleaseGesture::Release::TURN) {
        state_.visible = false;
        state_.bump();
        return {SequencerContextSelectorAction::APPLY_CONTEXT, selected};
    }
    if (selected != origin) {
        state_.visible = false;
        state_.bump();
        return {};
    }
    if (selected == Focus::STEP) {
        state_.visible = false;
        state_.bump();
        return {
            SequencerContextSelectorAction::OPEN_STEP_EDITOR,
            selected,
            previewTarget,
            previewAddSlot,
        };
    }
    if (selected == Focus::PAGE) {
        state_.visible = false;
        state_.bump();
        return {
            SequencerContextSelectorAction::OPEN_PATTERN_EDITOR,
            selected,
            previewTarget,
            previewAddSlot,
        };
    }

    state_.visible = false;
    state_.bump();
    return {
        SequencerContextSelectorAction::OPEN_TRACK_EDITOR,
        selected,
        previewTarget,
        previewAddSlot,
    };
}

FLASHMEM void SequencerContextSelectorWorkflow::update() {
    if (gesture_.active() && !state_.visible) {
        gesture_.cancel();
        press_context_ = 0U;
        press_target_ = 0U;
    }
}

FLASHMEM void SequencerContextSelectorWorkflow::cancel() {
    gesture_.cancel();
    press_context_ = 0U;
    press_target_ = 0U;
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
