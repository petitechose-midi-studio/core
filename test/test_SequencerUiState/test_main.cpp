#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <array>
#include <type_traits>

#include "../../src/state/sequencer/SequencerUiState.hpp"

namespace {
using oc::note::sequencer::StepBitMask128;

void test_inline_feedback_expires_steps_independently() {
    core::state::sequencer::SequencerStepInlineFeedbackState state;

    state.show(1, core::state::sequencer::StepProperty::NOTE, 100);
    state.show(3, core::state::sequencer::StepProperty::VELOCITY, 200);

    assert(state.visible.get());
    assert(state.property.get() == core::state::sequencer::StepProperty::VELOCITY);
    assert(state.touchedMask.get() ==
           StepBitMask128::fromLower64((1ULL << 1) | (1ULL << 3)));

    state.update(799);
    assert(state.visible.get());
    assert(state.touchedMask.get() ==
           StepBitMask128::fromLower64((1ULL << 1) | (1ULL << 3)));

    state.update(800);
    assert(state.visible.get());
    assert(state.touchedMask.get() == StepBitMask128::fromLower64(1ULL << 3));
    assert(state.property.get() == core::state::sequencer::StepProperty::VELOCITY);

    state.update(900);
    assert(!state.visible.get());
    assert(state.touchedMask.get() == StepBitMask128{});

    std::cout << "[PASS] test_inline_feedback_expires_steps_independently\n";
}

void test_inline_feedback_reset_clears_state() {
    core::state::sequencer::SequencerStepInlineFeedbackState state;

    state.show(5, core::state::sequencer::StepProperty::PROBABILITY, 50);
    assert(state.visible.get());

    state.reset();

    assert(!state.visible.get());
    assert(state.touchedMask.get() == StepBitMask128{});
    assert(state.property.get() == core::state::sequencer::StepProperty::NOTE);
    for (uint32_t value : state.hideAtMs) {
        assert(value == 0);
    }

    std::cout << "[PASS] test_inline_feedback_reset_clears_state\n";
}

void test_pattern_quick_controls_reset_clears_physical_hold_layer() {
    core::state::sequencer::SequencerPatternQuickControlsState state;

    state.selecting.set(true);
    state.physicalHoldActive.set(true);
    state.feedbackVisible.set(true);
    state.hideAtMs = 1234;
    state.offsetSteps.set(3);

    state.reset();

    assert(!state.selecting.get());
    assert(!state.physicalHoldActive.get());
    assert(!state.feedbackVisible.get());
    assert(state.hideAtMs == 0);
    assert(state.offsetSteps.get() == 0);

    std::cout << "[PASS] test_pattern_quick_controls_reset_clears_physical_hold_layer\n";
}

void test_pattern_quick_controls_feedback_shows_and_expires() {
    core::state::sequencer::SequencerPatternQuickControlsState state;

    state.showFeedback(100);
    assert(state.feedbackVisible.get());

    state.update(
        100 + core::state::sequencer::SequencerPatternQuickControlsState::DISPLAY_HOLD_MS - 1
    );
    assert(state.feedbackVisible.get());

    state.update(
        100 + core::state::sequencer::SequencerPatternQuickControlsState::DISPLAY_HOLD_MS
    );
    assert(!state.feedbackVisible.get());
    assert(state.hideAtMs == 0);

    std::cout << "[PASS] test_pattern_quick_controls_feedback_shows_and_expires\n";
}

void test_history_feedback_shows_and_expires() {
    core::state::sequencer::SequencerHistoryFeedbackState state;

    state.show("UNDO T01", "Step 01 Pitch", "D4 -> C3", 100);

    assert(state.visible.get());
    assert(std::strcmp(state.line1.data(), "UNDO T01") == 0);
    assert(std::strcmp(state.line2.data(), "Step 01 Pitch") == 0);
    assert(std::strcmp(state.line3.data(), "D4 -> C3") == 0);

    state.update(100 + core::state::sequencer::SequencerHistoryFeedbackState::DISPLAY_HOLD_MS - 1);
    assert(state.visible.get());

    state.update(100 + core::state::sequencer::SequencerHistoryFeedbackState::DISPLAY_HOLD_MS);
    assert(!state.visible.get());
    assert(std::strcmp(state.line1.data(), "") == 0);

    std::cout << "[PASS] test_history_feedback_shows_and_expires\n";
}

void test_track_paste_has_one_bounded_revision_subscription_surface() {
    using State = core::state::sequencer::SequencerTrackPasteUiState;
    static_assert(std::is_standard_layout_v<
        core::state::contextual::GuardedActionState>);
    static_assert(std::is_trivially_copyable_v<
        core::state::contextual::OperationFeedbackState>);
    static_assert(decltype(State::revision)::maxSubscribers() == 8);

    State state;
    std::array<oc::state::Subscription, 8> subscriptions{};
    uint8_t notifications = 0;
    for (auto& subscription : subscriptions) {
        subscription = state.revision.subscribe(
            [&notifications](const uint32_t&) { ++notifications; }
        );
        assert(subscription.isValid());
    }
    assert(state.revision.subscriberCount() == 8);
    state.bump();
    oc::state::NotificationQueue::instance().flush();
    assert(notifications == 8);

    state.plan.count = 1;
    state.detailVisible = true;
    state.reset();
    assert(state.revision.subscriberCount() == 8);
    assert(state.plan.count == 0);
    assert(!state.detailVisible);
}

}  // namespace

int main() {
    test_inline_feedback_expires_steps_independently();
    test_inline_feedback_reset_clears_state();
    test_pattern_quick_controls_reset_clears_physical_hold_layer();
    test_pattern_quick_controls_feedback_shows_and_expires();
    test_history_feedback_shows_and_expires();
    test_track_paste_has_one_bounded_revision_subscription_surface();

    std::cout << "\nAll SequencerUiState tests passed.\n";
    return 0;
}
