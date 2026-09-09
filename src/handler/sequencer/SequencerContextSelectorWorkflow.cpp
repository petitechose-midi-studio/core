#include "handler/sequencer/SequencerContextSelectorWorkflow.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler {

using Focus = core::state::StructureNavigationFocus;

FLASHMEM SequencerContextSelectorWorkflow::SequencerContextSelectorWorkflow(
    core::state::sequencer::SequencerContextSelectorState& state
) : state_(state) {}

FLASHMEM void SequencerContextSelectorWorkflow::press(
    Focus current,
    uint8_t previewTarget,
    bool includeLane
) {
    gesture_.press();
    state_.previewFocus = current == Focus::TRACK ||
            (!includeLane && current == Focus::LANE)
        ? Focus::PAGE
        : current;
    press_target_ = previewTarget;
    press_context_ = static_cast<uint8_t>(
        (static_cast<uint8_t>(state_.previewFocus) & 0x03U) |
        (includeLane ? 0x04U : 0U)
    );
    state_.visible = true;
    state_.bump();
}

FLASHMEM bool SequencerContextSelectorWorkflow::holdForSelection(
    Focus current,
    uint8_t previewTarget
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
        previewTarget == press_target_;
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
        (press_context_ & 0x04U) != 0U
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
        };
    }
    if (selected == Focus::PAGE) {
        state_.visible = false;
        state_.bump();
        return {
            SequencerContextSelectorAction::OPEN_PATTERN_EDITOR,
            selected,
            previewTarget,
        };
    }
    if (selected == Focus::LANE) {
        state_.visible = false;
        state_.bump();
        return {
            SequencerContextSelectorAction::OPEN_LANE_EDITOR,
            selected,
            previewTarget,
        };
    }

    state_.visible = false;
    state_.bump();
    return {};
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
    bool includeLane
) {
    // Musical order is Pattern(PAGE) -> [Lane] -> Step, with wrap.
    constexpr Focus order[] = {
        Focus::PAGE,
        Focus::LANE,
        Focus::STEP,
    };
    const int count = includeLane ? 3 : 2;
    int index = 0;
    for (int i = 0; i < count; ++i) {
        const Focus candidate = includeLane || i == 0 ? order[i] : order[i + 1];
        if (candidate == current) {
            index = i;
            break;
        }
    }
    const int next = (index + (direction > 0 ? 1 : count - 1)) % count;
    return includeLane || next == 0 ? order[next] : order[next + 1];
}

}  // namespace core::handler
