#include <cassert>
#include <iostream>

#include "handler/sequencer/SequencerContextSelectorWorkflow.hpp"

namespace {

static_assert(sizeof(core::handler::SequencerContextSelectorWorkflow) <= 24U);

using Action = core::handler::SequencerContextSelectorAction;
using Focus = core::state::StructureNavigationFocus;

void test_press_turn_release_applies_wrapped_preview() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::PAGE);
    assert(workflow.active());
    assert(state.visible);
    assert(state.previewFocus == Focus::PAGE);

    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::STEP);
    auto result = workflow.release();
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::STEP);
    assert(!state.visible);

    workflow.press(Focus::STEP);
    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::PAGE);
    assert(workflow.turn(-1.0f));
    assert(state.previewFocus == Focus::STEP);
    result = workflow.release();
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::STEP);
}

void test_hold_without_rotation_transfers_ownership_to_selection() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::PAGE);
    assert(workflow.holdForSelection(Focus::PAGE, 0U));
    assert(!workflow.active());
    assert(!state.visible);
    assert(workflow.release().action == Action::NONE);
}

void test_rotation_permanently_owns_the_gesture() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::PAGE);
    assert(workflow.turn(1.0f));
    assert(!workflow.holdForSelection(Focus::STEP, 0U));
    assert(workflow.active());
    const auto result = workflow.release();
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::STEP);
}

void test_tap_opens_the_editor_for_each_root_context() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::STEP);
    auto result = workflow.release();
    assert(result.action == Action::OPEN_STEP_EDITOR);
    assert(!state.visible);

    workflow.press(Focus::PAGE);
    result = workflow.release();
    assert(result.action == Action::OPEN_PATTERN_EDITOR);
    assert(result.focus == Focus::PAGE);
    assert(!state.visible);

    workflow.press(Focus::LANE, 3U, true);
    result = workflow.release();
    assert(result.action == Action::OPEN_LANE_EDITOR);
    assert(result.focus == Focus::LANE);
    assert(result.previewTarget == 3U);
    assert(!state.visible);
}

void test_tap_preserves_preview_intent_and_rejects_external_focus_drift() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::STEP, 9U);
    auto result = workflow.release();
    assert(result.action == Action::OPEN_STEP_EDITOR);
    assert(result.focus == Focus::STEP);
    assert(result.previewTarget == 9U);

    workflow.press(Focus::PAGE, 7U);
    state.previewFocus = Focus::TRACK;
    result = workflow.release();
    assert(result.action == Action::NONE);
    assert(!state.visible);

    workflow.press(Focus::STEP, 4U);
    assert(!workflow.holdForSelection(Focus::PAGE, 4U));
    assert(!workflow.active());
    assert(!state.visible);

    std::cout
        << "[PASS] selector tap preserves preview intent and rejects focus drift\n";
}

void test_exact_target_and_hidden_state_fail_closed() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::STEP, 0xE1U);
    auto result = workflow.release();
    assert(result.action == Action::OPEN_STEP_EDITOR);
    assert(result.previewTarget == 0xE1U);

    workflow.press(Focus::PAGE, 7U);
    state.reset();
    assert(!workflow.active());
    result = workflow.release();
    assert(result.action == Action::NONE);
    assert(!workflow.active());

    workflow.press(Focus::PAGE, 7U);
    state.reset();
    assert(!workflow.holdForSelection(Focus::PAGE, 7U));
    assert(!workflow.active());

    workflow.press(Focus::PAGE, 7U);
    state.reset();
    assert(!workflow.turn(1.0f));
    assert(!workflow.active());

    workflow.press(Focus::PAGE, 7U);
    state.reset();
    workflow.update();
    assert(!workflow.active());

    std::cout
        << "[PASS] selector preserves full targets and hidden state fails closed\n";
}

void test_child_selector_cycles_pattern_and_step_without_track() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::PAGE);
    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::STEP);
    auto result = workflow.release();
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::STEP);

    workflow.press(Focus::STEP);
    assert(workflow.turn(-1.0f));
    assert(state.previewFocus == Focus::PAGE);
    result = workflow.release();
    assert(result.action == Action::APPLY_CONTEXT);
    assert(result.focus == Focus::PAGE);

    // A stale Track focus is never exposed inside child content.
    workflow.press(Focus::TRACK);
    assert(state.previewFocus == Focus::PAGE);
    workflow.cancel();

    // Lane is Drum-root-only and is normalized out of child content.
    workflow.press(Focus::LANE);
    assert(state.previewFocus == Focus::PAGE);
    workflow.cancel();
}

void test_drum_root_selector_cycles_pattern_lane_and_step() {
    core::state::sequencer::SequencerContextSelectorState state;
    core::handler::SequencerContextSelectorWorkflow workflow(state);

    workflow.press(Focus::PAGE, 0U, true);
    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::LANE);
    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::STEP);
    assert(workflow.turn(1.0f));
    assert(state.previewFocus == Focus::PAGE);
    workflow.cancel();

    // Instrument roots never expose a stale Drum Lane focus.
    workflow.press(Focus::LANE);
    assert(state.previewFocus == Focus::PAGE);
    assert(workflow.release().action == Action::OPEN_PATTERN_EDITOR);
}

}  // namespace

int main() {
    test_press_turn_release_applies_wrapped_preview();
    test_hold_without_rotation_transfers_ownership_to_selection();
    test_rotation_permanently_owns_the_gesture();
    test_tap_opens_the_editor_for_each_root_context();
    test_tap_preserves_preview_intent_and_rejects_external_focus_drift();
    test_exact_target_and_hidden_state_fail_closed();
    test_child_selector_cycles_pattern_and_step_without_track();
    test_drum_root_selector_cycles_pattern_lane_and_step();
    std::cout << "All SequencerContextSelectorWorkflow tests passed.\n";
    return 0;
}
