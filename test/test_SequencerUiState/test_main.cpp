#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <array>
#include <type_traits>

#include "../../src/state/sequencer/SequencerPresetLibraryEntryPolicy.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
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

void test_pattern_quick_controls_reset_clears_transient_state() {
    core::state::sequencer::SequencerPatternQuickControlsState state;

    state.selecting.set(true);
    state.feedbackVisible.set(true);
    state.hideAtMs = 1234;
    state.offsetSteps.set(3);

    state.reset();

    assert(!state.selecting.get());
    assert(!state.feedbackVisible.get());
    assert(state.hideAtMs == 0);
    assert(state.offsetSteps.get() == 0);

    std::cout << "[PASS] test_pattern_quick_controls_reset_clears_transient_state\n";
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

void test_context_selector_state_is_bounded_and_resettable() {
    using State = core::state::sequencer::SequencerContextSelectorState;
    static_assert(sizeof(State) <= 128U);
    static_assert(decltype(State::revision)::maxSubscribers() == 2U);

    State state;
    state.visible = true;
    state.previewFocus = core::state::StructureNavigationFocus::TRACK;
    state.reset();

    assert(!state.visible);
    assert(state.previewFocus == core::state::StructureNavigationFocus::PAGE);
}

void test_chord_sub_editor_has_one_atomic_observation_surface() {
    using Editor =
        core::state::sequencer::SequencerChordEditorState;
    using SubEditor =
        core::state::sequencer::SequencerChordSubEditorState;

    static_assert(sizeof(SubEditor) == 4U);
    static_assert(decltype(Editor::active)::maxSubscribers() == 1U);
    static_assert(decltype(Editor::focusedField)::maxSubscribers() == 1U);
    static_assert(decltype(Editor::subEditor)::maxSubscribers() == 1U);

    Editor editor;
    uint8_t notifications = 0;
    auto subscription = editor.subEditor.subscribe(
        [&notifications](const SubEditor&) { ++notifications; }
    );
    assert(subscription.isValid());

    auto next = editor.subEditor.get();
    next.formulaEditorActive = true;
    next.focusedFormulaItem = 3;
    editor.subEditor.set(next);
    oc::state::NotificationQueue::instance().flush();

    assert(notifications == 1);
    assert(editor.subEditor.get().formulaEditorActive);
    assert(editor.subEditor.get().focusedFormulaItem == 3);
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
    assert(!state.plan.hasEntries());
    assert(!state.detailVisible);
}

void test_preset_library_keeps_only_the_active_domain_payload() {
    namespace seq = core::state::sequencer;
    using Library = seq::SequencerPresetLibrarySessionState;
    using Payload = seq::SequencerPresetLibraryPayload;
    using StepPayload = seq::SequencerStepPresetLibraryState;
    using ChordPayload = seq::SequencerChordPresetLibraryState;

    static_assert(
        sizeof(Payload) < sizeof(StepPayload) + sizeof(ChordPayload),
        "The shared preset library must not retain both domain descriptors"
    );

    Library library;
    library.open(
        seq::SequencerPresetLibraryMode::LOAD,
        seq::SequencerPresetLibraryKind::STEP
    );
    assert(std::holds_alternative<StepPayload>(library.payload));
    library.step().target.valid = true;
    library.entryCount.set(1U);
    library.setEntry(0U, "stale-step", "Stale Step", true);
    library.hasPreviousPage.set(true);
    library.hasNextPage.set(true);
    library.totalEntryCount.set(17U);
    library.selectedIndex.set(0U);
    assert(library.selectedItemIsExistingAsset());
    assert(!library.selectedItemIsNewAsset());

    library.mode.set(seq::SequencerPresetLibraryMode::SAVE);
    assert(library.selectedItemIsNewAsset());
    assert(!library.selectedItemIsExistingAsset());
    library.selectedIndex.set(1U);
    assert(library.selectedItemIsExistingAsset());

    library.open(
        seq::SequencerPresetLibraryMode::SAVE,
        seq::SequencerPresetLibraryKind::CHORD
    );
    assert(library.visible.get());
    assert(
        library.libraryKind.get() ==
        seq::SequencerPresetLibraryKind::CHORD
    );
    assert(std::holds_alternative<ChordPayload>(library.payload));
    assert(!library.chord().target.valid);
    assert(library.entryCount.get() == 0U);
    assert(library.totalEntryCount.get() == 0U);
    assert(!library.hasPreviousPage.get());
    assert(!library.hasNextPage.get());
    assert(std::strcmp(library.entryId(0U), "") == 0);
    assert(std::strcmp(library.entryName(0U), "") == 0);
    assert(!library.entryHasReadableMetadata(0U));
    library.chord().target.valid = true;

    library.reset();
    assert(!library.visible.get());
    assert(
        library.libraryKind.get() ==
        seq::SequencerPresetLibraryKind::STEP
    );
    assert(std::holds_alternative<StepPayload>(library.payload));
    assert(!library.step().target.valid);

    std::cout
        << "[PASS] preset library retains only its active domain payload\n";
}

void test_preset_library_entry_policy_matches_the_visible_editor_surface() {
    namespace policy =
        core::state::sequencer::preset_library_entry_policy;
    core::state::sequencer::SequencerState sequencer;

    assert(policy::entryKind(sequencer) == policy::EntryKind::NONE);
    sequencer.stepEdit.visible.set(true);
    assert(policy::canOpenStepPresets(sequencer));
    assert(!policy::canOpenChordPresets(sequencer));

    sequencer.stepContentDraft.active.set(true);
    assert(policy::entryKind(sequencer) == policy::EntryKind::NONE);

    sequencer.stepEdit.chordEditor.active.set(true);
    assert(policy::canOpenChordPresets(sequencer));
    assert(!policy::canOpenStepPresets(sequencer));

    auto subEditor = sequencer.stepEdit.chordEditor.subEditor.get();
    subEditor.formulaEditorActive = true;
    sequencer.stepEdit.chordEditor.subEditor.set(subEditor);
    assert(policy::entryKind(sequencer) == policy::EntryKind::NONE);

    subEditor.formulaEditorActive = false;
    subEditor.sourceSelectorActive = true;
    sequencer.stepEdit.chordEditor.subEditor.set(subEditor);
    assert(policy::entryKind(sequencer) == policy::EntryKind::NONE);

    subEditor.sourceSelectorActive = false;
    sequencer.stepEdit.chordEditor.subEditor.set(subEditor);
    sequencer.stepContentDraft.exitPromptVisible.set(true);
    assert(policy::entryKind(sequencer) == policy::EntryKind::NONE);

    std::cout
        << "[PASS] preset library entry policy follows the visible editor surface\n";
}

}  // namespace

int main() {
    test_inline_feedback_expires_steps_independently();
    test_inline_feedback_reset_clears_state();
    test_pattern_quick_controls_reset_clears_transient_state();
    test_pattern_quick_controls_feedback_shows_and_expires();
    test_context_selector_state_is_bounded_and_resettable();
    test_chord_sub_editor_has_one_atomic_observation_surface();
    test_history_feedback_shows_and_expires();
    test_track_paste_has_one_bounded_revision_subscription_surface();
    test_preset_library_keeps_only_the_active_domain_payload();
    test_preset_library_entry_policy_matches_the_visible_editor_surface();

    std::cout << "\nAll SequencerUiState tests passed.\n";
    return 0;
}
