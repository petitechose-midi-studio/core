#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/context/standalone/SequencerCcLaneOverlayVisuals.hpp"
#include "../../src/state/contextual/GuardedActionState.hpp"
#include "../../src/state/contextual/OperationFeedbackState.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/ui/sequencer/SequencerStepPresetPickerPresentation.hpp"

namespace {

namespace presenter = core::ui::sequencer;
using core::state::sequencer::SequencerStepPresetCompatibility;
using core::state::sequencer::SequencerStepPresetFootprint;
using core::state::sequencer::SequencerStepPresetPickerMode;

void test_picker_list_uses_semantic_names_and_disambiguates_duplicates() {
    core::state::sequencer::SequencerState sequencer;
    auto& picker = sequencer.stepPresetPicker;
    picker.open(SequencerStepPresetPickerMode::SAVE);
    picker.setEntry(0, "preset-a", "Pulse", true);
    picker.setEntry(1, "preset-b", "Pulse", true);
    picker.setEntry(2, "unnamed-id", "", false);
    picker.entryCount.set(3);
    picker.totalEntryCount.set(3);

    auto data = presenter::buildSequencerStepPresetPickerPresentation(
        sequencer
    );
    assert(data.visible);
    assert(std::strcmp(data.title.data(), "Save Step Preset") == 0);
    assert(data.itemCount == 4);
    assert(std::strcmp(data.items[0], "+  New Step Preset") == 0);
    assert(std::strcmp(data.items[1], "Pulse  [preset-a]") == 0);
    assert(std::strcmp(data.items[2], "Pulse  [preset-b]") == 0);
    assert(std::strcmp(data.items[3], "unnamed-id") == 0);

    picker.hasPreviousPage.set(true);
    data = presenter::buildSequencerStepPresetPickerPresentation(sequencer);
    assert(data.itemCount == 3);
    assert(std::strcmp(data.items[0], "Pulse  [preset-a]") == 0);

    std::cout << "[PASS] test_picker_list_uses_semantic_names_and_disambiguates_duplicates\n";
}

void test_picker_detail_is_temporary_semantic_impact_projection() {
    core::state::sequencer::SequencerState sequencer;
    auto& picker = sequencer.stepPresetPicker;
    picker.open(SequencerStepPresetPickerMode::LOAD);
    picker.setEntry(0, "preset-mixed", "Orbit", true);
    picker.entryCount.set(1);
    picker.detailVisible.set(true);
    picker.detailFocus.set(3);
    picker.frozenTarget.valid = true;
    std::strncpy(
        picker.frozenTarget.contextLabel,
        "T1 Root S06",
        sizeof(picker.frozenTarget.contextLabel) - 1U
    );
    auto& descriptor = picker.descriptor;
    descriptor.valid = true;
    std::strncpy(descriptor.semanticName, "Orbit", sizeof(descriptor.semanticName) - 1U);
    std::strncpy(
        descriptor.contentSummary,
        "Root - Values - Micro",
        sizeof(descriptor.contentSummary) - 1U
    );
    std::strncpy(
        descriptor.replaceFacts,
        "Step values + child graph",
        sizeof(descriptor.replaceFacts) - 1U
    );
    std::strncpy(
        descriptor.preserveFacts,
        "Track route, scale, other steps",
        sizeof(descriptor.preserveFacts) - 1U
    );
    descriptor.mixedPitchPolicy = true;
    std::strncpy(
        descriptor.adaptationSummary,
        "C major -> D minor",
        sizeof(descriptor.adaptationSummary) - 1U
    );
    std::strncpy(
        descriptor.previewSummary,
        "Preview 2/4 - D4",
        sizeof(descriptor.previewSummary) - 1U
    );
    descriptor.compatibility = SequencerStepPresetCompatibility::WARNING_ADAPTED;
    std::strncpy(
        descriptor.compatibilityReason,
        "Pitch follows destination scale",
        sizeof(descriptor.compatibilityReason) - 1U
    );

    const auto data = presenter::buildSequencerStepPresetPickerPresentation(
        sequencer
    );
    assert(data.itemCount == 5);
    assert(data.selectedIndex == 3);
    assert(std::strcmp(data.title.data(), "Orbit") == 0);
    assert(std::strstr(data.items[0], "Target") != nullptr);
    assert(std::strstr(data.items[1], "Content") != nullptr);
    assert(std::strstr(data.items[2], "Add content") != nullptr);
    assert(std::strstr(data.items[2], "keeps") != nullptr);
    assert(std::strstr(data.items[3], "Mixed; scale") != nullptr);
    assert(std::strstr(data.items[4], "Preview 2/4") != nullptr);
    assert(std::strstr(data.meta.data(), "Adapted") != nullptr);

    picker.descriptor.footprint = SequencerStepPresetFootprint::REPLACE;
    const auto replaceData = presenter::buildSequencerStepPresetPickerPresentation(
        sequencer
    );
    assert(std::strstr(replaceData.items[2], "Replace step") != nullptr);

    std::cout << "[PASS] test_picker_detail_is_temporary_semantic_impact_projection\n";
}

void test_picker_action_strip_projects_guard_and_temporary_outcome() {
    core::state::sequencer::SequencerState sequencer;
    auto& picker = sequencer.stepPresetPicker;
    picker.open(SequencerStepPresetPickerMode::LOAD);
    picker.setEntry(0, "preset-a", "Pulse", true);
    picker.entryCount.set(1);
    picker.frozenTarget.valid = true;
    picker.descriptor.valid = true;
    picker.descriptor.compatibility = SequencerStepPresetCompatibility::READY;
    picker.descriptor.footprint = SequencerStepPresetFootprint::FREE;

    auto props = presenter::buildSequencerStepPresetActionPresentation(
        picker
    );
    assert(props.visual == presenter::SequencerStepPresetActionVisual::ACTIVE);
    assert(props.tone == presenter::SequencerStepPresetActionTone::POSITIVE);
    assert(!props.overwriteIcon);

    picker.descriptor.footprint = SequencerStepPresetFootprint::REPLACE;
    props = presenter::buildSequencerStepPresetActionPresentation(picker);
    assert(props.visual == presenter::SequencerStepPresetActionVisual::ACTIVE);
    assert(props.tone == presenter::SequencerStepPresetActionTone::WARNING);
    assert(props.overwriteIcon);

    core::state::contextual::GuardedActionState guard{};
    guard.phase = core::state::contextual::GuardedActionPhase::PRESSED;
    guard.pressedAtMs = 120;
    guard.guardDurationMs = 1000;
    picker.actionGuard.set(guard);
    props = presenter::buildSequencerStepPresetActionPresentation(picker);
    assert(props.visual == presenter::SequencerStepPresetActionVisual::PRESSED);
    assert(props.holdActive);
    assert(props.holdStartedAtMs == 120);

    core::state::contextual::OperationFeedbackState feedback{};
    feedback.active = true;
    feedback.status = core::state::contextual::OperationFeedbackStatus::APPLIED;
    picker.operationFeedback.set(feedback);
    props = presenter::buildSequencerStepPresetActionPresentation(picker);
    assert(props.visual == presenter::SequencerStepPresetActionVisual::APPLIED);
    assert(props.tone == presenter::SequencerStepPresetActionTone::POSITIVE);
    assert(props.showLabel);
    assert(props.statusIcon != nullptr);
    assert(std::strcmp(props.label.data(), "Applied") == 0);

    const auto data = presenter::buildSequencerStepPresetPickerPresentation(
        sequencer
    );
    assert(std::strcmp(data.meta.data(), "Applied") == 0);

    std::cout << "[PASS] test_picker_action_strip_projects_guard_and_temporary_outcome\n";
}

void test_picker_explains_capacity_storage_and_queued_states() {
    core::state::sequencer::SequencerState sequencer;
    auto& picker = sequencer.stepPresetPicker;
    picker.open(SequencerStepPresetPickerMode::LOAD);
    picker.setEntry(0, "preset-a", "Pulse", true);
    picker.entryCount.set(1);
    picker.frozenTarget.valid = true;
    picker.descriptor.valid = true;
    picker.descriptor.compatibility = SequencerStepPresetCompatibility::BLOCKED_CAPACITY;
    picker.hasNextPage.set(true);
    std::strncpy(
        picker.descriptor.compatibilityReason,
        "Graph capacity exceeded",
        sizeof(picker.descriptor.compatibilityReason) - 1U
    );

    auto data = presenter::buildSequencerStepPresetPickerPresentation(sequencer);
    assert(std::strstr(data.meta.data(), "Graph full") != nullptr);
    assert(std::strstr(data.meta.data(), ">") != nullptr);

    core::state::contextual::OperationFeedbackState feedback{};
    feedback.active = true;
    feedback.status = core::state::contextual::OperationFeedbackStatus::FAILED;
    feedback.reason = core::state::contextual::ContextActionReason::STORAGE_UNAVAILABLE;
    picker.operationFeedback.set(feedback);
    data = presenter::buildSequencerStepPresetPickerPresentation(sequencer);
    assert(std::strcmp(data.meta.data(), "Storage unavailable") == 0);

    feedback.status = core::state::contextual::OperationFeedbackStatus::QUEUED;
    feedback.reason = core::state::contextual::ContextActionReason::PENDING;
    picker.operationFeedback.set(feedback);
    data = presenter::buildSequencerStepPresetPickerPresentation(sequencer);
    assert(std::strcmp(data.meta.data(), "Queued for loop boundary") == 0);
    const auto action = presenter::buildSequencerStepPresetActionPresentation(
        picker
    );
    assert(action.showLabel);
    assert(action.statusIcon != nullptr);
    assert(std::strcmp(action.label.data(), "Queued") == 0);

    std::cout
        << "[PASS] test_picker_explains_capacity_storage_and_queued_states\n";
}

void test_cc_lane_guard_visual_is_scoped_to_the_matching_action() {
    namespace contextual = core::state::contextual;
    namespace visuals =
        core::context::standalone::cc_lane_overlay_visuals;
    contextual::ContextActionVariant remove{
        .action = contextual::ContextActionId::REMOVE,
        .availability = contextual::ContextActionAvailability::AVAILABLE,
    };
    contextual::ContextActionVariant clear{
        .action = contextual::ContextActionId::CLEAR,
        .availability = contextual::ContextActionAvailability::AVAILABLE,
    };
    contextual::GuardedActionState guard{};
    guard.phase = contextual::GuardedActionPhase::PRESSED;
    contextual::OperationFeedbackState feedback{};
    feedback.active = true;
    feedback.action = contextual::ContextActionId::REMOVE;
    feedback.status = contextual::OperationFeedbackStatus::PRESSED;

    assert(visuals::stripVisual(remove, guard, feedback) ==
           core::ui::ContextActionStripVisualState::PRESSED);
    assert(visuals::stripVisual(clear, guard, feedback) ==
           core::ui::ContextActionStripVisualState::ACTIVE);

    guard.phase = contextual::GuardedActionPhase::ARMED;
    assert(visuals::stripVisual(remove, guard, feedback) ==
           core::ui::ContextActionStripVisualState::ARMED);
    assert(visuals::stripVisual(clear, guard, feedback) ==
           core::ui::ContextActionStripVisualState::ACTIVE);

    std::cout
        << "[PASS] test_cc_lane_guard_visual_is_scoped_to_the_matching_action\n";
}

void test_cc_lane_feedback_status_icon_is_scoped_to_matching_action() {
    namespace contextual = core::state::contextual;
    namespace visuals =
        core::context::standalone::cc_lane_overlay_visuals;
    contextual::ContextActionVariant apply{
        .action = contextual::ContextActionId::APPLY,
        .availability = contextual::ContextActionAvailability::AVAILABLE,
        .visual = {
            .icon = contextual::ContextIconId::APPLY,
            .tone = contextual::ContextTone::GREEN,
        },
    };
    contextual::ContextActionVariant remove{
        .action = contextual::ContextActionId::REMOVE,
        .availability = contextual::ContextActionAvailability::AVAILABLE,
        .visual = {
            .icon = contextual::ContextIconId::REMOVE,
            .tone = contextual::ContextTone::RED,
        },
    };
    contextual::OperationFeedbackState feedback{};
    feedback.active = true;
    feedback.action = contextual::ContextActionId::APPLY;

    feedback.status = contextual::OperationFeedbackStatus::FAILED;
    auto policy = visuals::projectedVisualPolicy(apply, feedback);
    assert(policy.icon == contextual::ContextIconId::ERROR);
    assert(policy.tone == contextual::ContextTone::RED);

    feedback.status = contextual::OperationFeedbackStatus::WARNING;
    policy = visuals::projectedVisualPolicy(apply, feedback);
    assert(policy.icon == contextual::ContextIconId::WARNING);
    assert(policy.tone == contextual::ContextTone::AMBER);

    feedback.status = contextual::OperationFeedbackStatus::CONFLICT;
    policy = visuals::projectedVisualPolicy(apply, feedback);
    assert(policy.icon == contextual::ContextIconId::CONFLICT);

    feedback.status = contextual::OperationFeedbackStatus::QUEUED;
    policy = visuals::projectedVisualPolicy(apply, feedback);
    assert(policy.icon == contextual::ContextIconId::QUEUED);

    // A feedback result may only replace the icon/tone of its own slot.
    policy = visuals::projectedVisualPolicy(remove, feedback);
    assert(policy.icon == contextual::ContextIconId::REMOVE);
    assert(policy.tone == contextual::ContextTone::RED);

    std::cout
        << "[PASS] test_cc_lane_feedback_status_icon_is_scoped_to_matching_action\n";
}

}  // namespace

int main() {
    test_picker_list_uses_semantic_names_and_disambiguates_duplicates();
    test_picker_detail_is_temporary_semantic_impact_projection();
    test_picker_action_strip_projects_guard_and_temporary_outcome();
    test_picker_explains_capacity_storage_and_queued_states();
    test_cc_lane_guard_visual_is_scoped_to_the_matching_action();
    test_cc_lane_feedback_status_icon_is_scoped_to_matching_action();
    std::cout << "\nAll SequencerOverlayPresenterFormatters tests passed.\n";
    return 0;
}
