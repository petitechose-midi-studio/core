#include <cassert>
#include <iostream>

#include "handler/sequencer/SequencerContextSelectorWorkflow.hpp"

namespace {

static_assert(sizeof(core::handler::SequencerContextSelectorWorkflow) <= 24U);

using Action = core::handler::SequencerContextSelectorAction;
using Focus = core::state::StructureNavigationFocus;
using Feedback = core::state::sequencer::SequencerContextSelectorFeedback;

void test_press_turn_release_applies_wrapped_preview() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::PAGE);
    assert(workflow.active());
    assert(state.visible);
    assert(state.previewFocus == Focus::PAGE);

    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::STEP);
    auto result = workflow.release(10U);
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::STEP);
    assert(!state.visible);

    workflow.press(Focus::STEP);
    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::TRACK);
    assert(workflow.turn(-1.0f));
    assert(state.previewFocus == Focus::STEP);
    result = workflow.release(20U);
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::STEP);
}

void test_hold_without_rotation_transfers_ownership_to_selection() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::TRACK);
    assert(workflow.holdForSelection());
    assert(!workflow.active());
    assert(!state.visible);
    assert(workflow.release(100U).action == Action::NONE);
}

void test_rotation_permanently_owns_the_gesture() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::TRACK);
    assert(workflow.turn(1.0f));
    assert(!workflow.holdForSelection());
    assert(workflow.active());
    const auto result = workflow.release(200U);
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::PAGE);
}

void test_tap_opens_the_editor_for_each_root_context() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::STEP);
    auto result = workflow.release(300U);
    assert(result.action == Action::OPEN_STEP_EDITOR);
    assert(!state.visible);

    workflow.press(Focus::PAGE);
    result = workflow.release(400U);
    assert(result.action == Action::OPEN_PATTERN_EDITOR);
    assert(result.focus == Focus::PAGE);
    assert(!state.visible);
    assert(state.feedback == Feedback::NONE);

    workflow.press(Focus::TRACK);
    result = workflow.release(1400U);
    assert(result.action == Action::OPEN_TRACK_EDITOR);
    assert(result.focus == Focus::TRACK);
    assert(!state.visible);
    assert(state.feedback == Feedback::NONE);
}

void test_child_selector_cycles_pattern_and_step_without_track() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::PAGE, false);
    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::STEP);
    auto result = workflow.release(10U);
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::STEP);

    workflow.press(Focus::STEP, false);
    assert(workflow.turn(-1.0f));
    assert(state.previewFocus == Focus::PAGE);
    result = workflow.release(20U);
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::PAGE);

    // A stale Track focus is never exposed inside child content.
    workflow.press(Focus::TRACK, false);
    assert(state.previewFocus == Focus::PAGE);
    workflow.cancel();
}

}  // namespace

int main() {
    test_press_turn_release_applies_wrapped_preview();
    test_hold_without_rotation_transfers_ownership_to_selection();
    test_rotation_permanently_owns_the_gesture();
    test_tap_opens_the_editor_for_each_root_context();
    test_child_selector_cycles_pattern_and_step_without_track();
    std::cout << "All SequencerContextSelectorWorkflow tests passed.\n";
    return 0;
}
