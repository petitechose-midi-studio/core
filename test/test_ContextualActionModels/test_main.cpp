#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "state/contextual/ContextualActionModels.hpp"

namespace {

namespace contextual = core::state::contextual;

static_assert(std::is_standard_layout_v<contextual::ContextEntityRef>);
static_assert(std::is_trivially_copyable_v<contextual::ContextEntityRef>);
static_assert(std::is_standard_layout_v<contextual::ContextActionVisualPolicy>);
static_assert(std::is_trivially_copyable_v<contextual::ContextActionVisualPolicy>);
static_assert(std::is_standard_layout_v<contextual::ContextActionVariant>);
static_assert(std::is_trivially_copyable_v<contextual::ContextActionVariant>);
static_assert(std::is_standard_layout_v<contextual::ContextActionSpec>);
static_assert(std::is_trivially_copyable_v<contextual::ContextActionSpec>);
static_assert(std::is_standard_layout_v<contextual::GuardedActionState>);
static_assert(std::is_trivially_copyable_v<contextual::GuardedActionState>);
static_assert(std::is_standard_layout_v<contextual::OperationFeedbackState>);
static_assert(std::is_trivially_copyable_v<contextual::OperationFeedbackState>);
static_assert(std::is_standard_layout_v<contextual::ContextPath>);
static_assert(std::is_trivially_copyable_v<contextual::ContextPath>);

contextual::ContextEntityRef trackRef(uint16_t track) {
    contextual::ContextEntityRef ref;
    ref.kind = contextual::ContextEntityKind::TRACK;
    ref.track = track;
    return ref;
}

void test_action_spec_keeps_tap_and_hold_semantics_independent() {
    contextual::ContextActionSpec spec;
    spec.tap.action = contextual::ContextActionId::COPY;
    spec.tap.impact = contextual::ContextActionImpact::NON_MUTATING;
    spec.tap.availability = contextual::ContextActionAvailability::AVAILABLE;
    spec.tap.reason = contextual::ContextActionReason::NONE;
    spec.tap.visual = {
        contextual::ContextIconId::COPY,
        contextual::ContextTone::BLUE,
    };
    spec.hold.action = contextual::ContextActionId::PASTE;
    spec.hold.impact = contextual::ContextActionImpact::OVERWRITE;
    spec.hold.availability = contextual::ContextActionAvailability::DISABLED;
    spec.hold.reason = contextual::ContextActionReason::SAME_SOURCE_TARGET;
    spec.hold.visual = {
        contextual::ContextIconId::PASTE,
        contextual::ContextTone::AMBER,
    };
    spec.scope = contextual::ContextScope::TRACK;
    spec.source = trackRef(1);
    spec.target = trackRef(4);
    spec.guard = {contextual::ContextGuardKind::HOLD, 1000};

    assert(contextual::hasTapAction(spec));
    assert(contextual::hasHoldAction(spec));
    assert(contextual::canExecute(spec.tap));
    assert(!contextual::canExecute(spec.hold));
    assert(contextual::requiresGuard(spec));
    assert(spec.tap.visual.icon == contextual::ContextIconId::COPY);
    assert(spec.hold.visual.tone == contextual::ContextTone::AMBER);
    assert(spec.hold.reason ==
           contextual::ContextActionReason::SAME_SOURCE_TARGET);
    assert(spec.source != spec.target);

    contextual::ContextActionSpec empty;
    assert(!contextual::hasTapAction(empty));
    assert(!contextual::hasHoldAction(empty));
    assert(!contextual::requiresGuard(empty));
    assert(!contextual::canExecute(empty.tap));

    std::cout << "[PASS] action spec independent tap/hold semantics\n";
}

void test_guarded_action_quick_release_is_a_tap() {
    contextual::GuardedActionState state;
    assert(contextual::beginGuardedActionPress(state, 100, 1000));
    assert(state.phase == contextual::GuardedActionPhase::PRESSED);
    assert(!contextual::updateGuardedAction(state, 500));
    assert(contextual::releaseGuardedAction(state, 500) ==
           contextual::GuardedActionRelease::TAP);
    assert(state.phase == contextual::GuardedActionPhase::IDLE);
    assert(state.progressPermille == 0);

    std::cout << "[PASS] guarded quick release\n";
}

void test_guarded_action_progresses_and_commits_at_deadline() {
    contextual::GuardedActionState state;
    assert(contextual::beginGuardedActionPress(state, 10, 1000));
    assert(contextual::armGuardedAction(state, 20));
    assert(state.phase == contextual::GuardedActionPhase::ARMED);
    assert(contextual::updateGuardedAction(state, 520));
    assert(state.progressPermille == 500);
    assert(contextual::updateGuardedAction(state, 1019));
    assert(state.progressPermille == 999);
    assert(contextual::updateGuardedAction(state, 1020));
    assert(state.phase == contextual::GuardedActionPhase::COMMITTED);
    assert(state.progressPermille ==
           contextual::GuardedActionState::COMPLETE_PERMILLE);
    assert(contextual::guardedActionTerminal(state));
    assert(contextual::releaseGuardedAction(state, 1020) ==
           contextual::GuardedActionRelease::COMMITTED);

    std::cout << "[PASS] guarded progress and deadline\n";
}

void test_guarded_action_early_release_cancels_without_tap() {
    contextual::GuardedActionState state;
    assert(contextual::beginGuardedActionPress(state, 100, 1000));
    assert(contextual::armGuardedAction(state, 200));
    assert(contextual::updateGuardedAction(state, 500));
    assert(state.progressPermille == 300);
    assert(contextual::releaseGuardedAction(state, 600) ==
           contextual::GuardedActionRelease::CANCELLED);
    assert(state.phase == contextual::GuardedActionPhase::CANCELLED);
    assert(contextual::guardedActionTerminal(state));
    assert(contextual::releaseGuardedAction(state, 700) ==
           contextual::GuardedActionRelease::NONE);

    contextual::resetGuardedAction(state);
    assert(contextual::beginGuardedActionPress(state, 800, 1000));
    assert(contextual::cancelGuardedAction(state));
    assert(state.phase == contextual::GuardedActionPhase::CANCELLED);

    std::cout << "[PASS] guarded early cancellation\n";
}

void test_guarded_action_zero_duration_invalid_transitions_and_wrap() {
    contextual::GuardedActionState state;
    assert(contextual::beginGuardedActionPress(state, 1, 0));
    assert(!contextual::beginGuardedActionPress(state, 2, 0));
    assert(contextual::armGuardedAction(state, 2));
    assert(state.phase == contextual::GuardedActionPhase::COMMITTED);
    assert(state.progressPermille == 1000);
    assert(!contextual::armGuardedAction(state, 3));
    assert(!contextual::cancelGuardedAction(state));

    contextual::resetGuardedAction(state);
    constexpr uint32_t start = UINT32_MAX - 100U;
    assert(contextual::beginGuardedActionPress(state, start, 1000));
    assert(contextual::armGuardedAction(state, start));
    assert(contextual::updateGuardedAction(state, 49));
    assert(state.progressPermille == 150);
    assert(contextual::releaseGuardedAction(state, 899) ==
           contextual::GuardedActionRelease::COMMITTED);

    std::cout << "[PASS] guarded zero duration, guards and wrap\n";
}

void test_feedback_duration_and_timestamp_wrap() {
    contextual::OperationFeedbackState state;
    contextual::setOperationFeedback(
        state,
        contextual::ContextActionId::PASTE,
        trackRef(1),
        trackRef(4),
        contextual::OperationFeedbackStatus::APPLIED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        100,
        1200
    );
    assert(state.active);
    assert(!contextual::updateOperationFeedback(state, 1299));
    assert(contextual::updateOperationFeedback(state, 1300));
    assert(!state.active);
    assert(state.status == contextual::OperationFeedbackStatus::NONE);

    constexpr uint32_t start = UINT32_MAX - 100U;
    contextual::setOperationFeedback(
        state,
        contextual::ContextActionId::SAVE,
        {},
        {},
        contextual::OperationFeedbackStatus::APPLIED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        start,
        200
    );
    assert(!contextual::updateOperationFeedback(state, 98));
    assert(contextual::updateOperationFeedback(state, 99));

    std::cout << "[PASS] feedback duration and wrap\n";
}

void test_feedback_expiry_policies_are_explicit() {
    contextual::OperationFeedbackState state;
    contextual::setOperationFeedback(
        state,
        contextual::ContextActionId::PREVIEW,
        trackRef(0),
        trackRef(2),
        contextual::OperationFeedbackStatus::PREVIEW,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::ON_NEXT_MEANINGFUL_INPUT,
        10
    );
    assert(!contextual::resolveOperationFeedback(state));
    assert(contextual::dismissOperationFeedbackOnMeaningfulInput(state));

    contextual::setOperationFeedback(
        state,
        contextual::ContextActionId::PASTE,
        trackRef(0),
        trackRef(2),
        contextual::OperationFeedbackStatus::QUEUED,
        contextual::ContextActionReason::PENDING,
        contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED,
        20
    );
    assert(!contextual::acknowledgeOperationFeedback(state));
    assert(contextual::resolveOperationFeedback(state));

    contextual::setOperationFeedback(
        state,
        contextual::ContextActionId::OVERWRITE,
        trackRef(0),
        trackRef(2),
        contextual::OperationFeedbackStatus::CONFLICT,
        contextual::ContextActionReason::CONFLICT,
        contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
        30
    );
    assert(contextual::acknowledgeOperationFeedback(state));

    contextual::setOperationFeedback(
        state,
        contextual::ContextActionId::SAVE,
        {},
        {},
        contextual::OperationFeedbackStatus::FAILED,
        contextual::ContextActionReason::STORAGE_UNAVAILABLE,
        contextual::OperationFeedbackExpiryPolicy::MANUAL,
        40
    );
    assert(!contextual::updateOperationFeedback(state, UINT32_MAX));
    assert(!contextual::dismissOperationFeedbackOnMeaningfulInput(state));
    assert(state.active);

    contextual::setOperationFeedback(
        state,
        contextual::ContextActionId::SAVE,
        {},
        {},
        contextual::OperationFeedbackStatus::NONE,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::MANUAL,
        50
    );
    assert(!state.active);
    assert(state.action == contextual::ContextActionId::NONE);

    std::cout << "[PASS] feedback explicit expiry policies\n";
}

void test_context_path_builds_bounded_semantic_segments() {
    contextual::ContextPath path;
    assert(contextual::contextPathEmpty(path));
    assert(std::strcmp(contextual::contextPathText(path), "") == 0);
    assert(contextual::appendIndexedContextPathSegment(path, "Track", 2));
    assert(contextual::appendIndexedContextPathSegment(path, "Pattern", 1));
    assert(contextual::appendIndexedContextPathSegment(path, "Step", 4));
    assert(std::strcmp(
               contextual::contextPathText(path),
               "Track 2 / Pattern 1 / Step 4"
           ) == 0);
    assert(path.length == 28);
    assert(path.segmentCount == 3);
    assert(!path.truncated);

    contextual::clearContextPath(path);
    assert(contextual::appendIndexedContextPathSegment(path, "Item", 65535));
    assert(std::strcmp(contextual::contextPathText(path), "Item 65535") == 0);

    std::cout << "[PASS] bounded semantic context path\n";
}

void test_context_path_rejects_overflow_atomically() {
    contextual::ContextPath path;
    assert(contextual::appendContextPathSegment(path, "Track 2"));
    const uint8_t originalLength = path.length;
    const uint8_t originalCount = path.segmentCount;
    constexpr char tooLong[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra";
    assert(!contextual::appendContextPathSegment(path, tooLong));
    assert(path.truncated);
    assert(path.length == originalLength);
    assert(path.segmentCount == originalCount);
    assert(std::strcmp(contextual::contextPathText(path), "Track 2") == 0);

    contextual::clearContextPath(path);
    assert(contextual::appendContextPathSegment(path, "A"));
    assert(contextual::appendContextPathSegment(path, "B"));
    assert(contextual::appendContextPathSegment(path, "C"));
    assert(contextual::appendContextPathSegment(path, "D"));
    assert(!contextual::appendContextPathSegment(path, "E"));
    assert(path.truncated);
    assert(path.segmentCount == contextual::ContextPath::MAX_SEGMENTS);
    assert(std::strcmp(contextual::contextPathText(path), "A / B / C / D") == 0);

    contextual::clearContextPath(path);
    assert(!contextual::appendContextPathSegment(path, nullptr));
    assert(!contextual::appendContextPathSegment(path, ""));
    assert(!path.truncated);
    assert(contextual::contextPathEmpty(path));

    std::cout << "[PASS] context path atomic overflow handling\n";
}

}  // namespace

int main() {
    test_action_spec_keeps_tap_and_hold_semantics_independent();
    test_guarded_action_quick_release_is_a_tap();
    test_guarded_action_progresses_and_commits_at_deadline();
    test_guarded_action_early_release_cancels_without_tap();
    test_guarded_action_zero_duration_invalid_transitions_and_wrap();
    test_feedback_duration_and_timestamp_wrap();
    test_feedback_expiry_policies_are_explicit();
    test_context_path_builds_bounded_semantic_segments();
    test_context_path_rejects_overflow_atomically();
    std::cout << "All contextual action model tests passed\n";
    return 0;
}
