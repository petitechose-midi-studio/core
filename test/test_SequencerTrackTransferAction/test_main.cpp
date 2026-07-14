#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "state/sequencer/SequencerTrackTransferAction.hpp"
#include "ui/sequencer/SequencerTrackPastePendingViewModel.hpp"
#include "validation/ux/SequencerTrackTransferSemanticProjection.hpp"

namespace {

using core::state::ClipboardTransferAvailability;
using core::state::ClipboardTransferPlan;
using core::state::ClipboardTransferReason;
using namespace core::state::contextual;

constexpr uint16_t GUARD_MS = 1000;

ClipboardTransferPlan basePlan() {
    ClipboardTransferPlan plan{};
    plan.availability = ClipboardTransferAvailability::READY;
    plan.reason = ClipboardTransferReason::NONE;
    plan.firstSource = 0;
    plan.firstTarget = 4;
    plan.lastTarget = 4;
    plan.sourceMask = 0x0001;
    plan.targetMask = 0x0010;
    plan.createMask = 0x0010;
    plan.count = 1;
    plan.sourceCount = 1;
    return plan;
}

void test_free_target_uses_copy_at_rest_and_guarded_green_paste() {
    const auto action =
        core::state::sequencer::buildSequencerTrackTransferActionSpec(
            basePlan(),
            4,
            true,
            GUARD_MS
        );

    assert(action.scope == ContextScope::TRACK);
    assert(action.source.track == 4);
    assert(action.target.track == 4);
    assert(action.target.item == 0x0010);
    assert(action.tap.action == ContextActionId::COPY);
    assert(canExecute(action.tap));
    assert(action.tap.visual.icon == ContextIconId::COPY);
    assert(action.hold.action == ContextActionId::PASTE);
    assert(canExecute(action.hold));
    assert(action.hold.impact == ContextActionImpact::CONSTRUCTIVE);
    assert(action.hold.visual.icon == ContextIconId::PASTE);
    assert(action.hold.visual.tone == ContextTone::GREEN);
    assert(requiresGuard(action));
    assert(action.guard.durationMs == GUARD_MS);

    std::cout << "[PASS] test_free_target_uses_copy_at_rest_and_guarded_green_paste\n";
}

void test_overwrite_and_no_route_are_explicit_amber_impacts() {
    auto overwrite = basePlan();
    overwrite.createMask = 0;
    overwrite.overwriteMask = overwrite.targetMask;
    auto action = core::state::sequencer::buildSequencerTrackTransferActionSpec(
        overwrite,
        4,
        true,
        GUARD_MS
    );
    assert(action.hold.impact == ContextActionImpact::OVERWRITE);
    assert(action.hold.availability == ContextActionAvailability::AVAILABLE);
    assert(action.hold.visual.tone == ContextTone::AMBER);

    auto noRoute = basePlan();
    noRoute.availability = ClipboardTransferAvailability::WARNING;
    noRoute.reason = ClipboardTransferReason::NO_ROUTE;
    action = core::state::sequencer::buildSequencerTrackTransferActionSpec(
        noRoute,
        4,
        true,
        GUARD_MS
    );
    assert(canExecute(action.hold));
    assert(action.hold.availability == ContextActionAvailability::WARNING);
    assert(action.hold.reason == ContextActionReason::NO_ROUTE);
    assert(action.hold.visual.tone == ContextTone::AMBER);

    std::cout << "[PASS] test_overwrite_and_no_route_are_explicit_amber_impacts\n";
}

void test_blocked_reasons_are_mapped_without_changing_copy() {
    auto sameTrack = basePlan();
    sameTrack.availability = ClipboardTransferAvailability::DISABLED;
    sameTrack.reason = ClipboardTransferReason::SAME_TRACK;
    const auto action =
        core::state::sequencer::buildSequencerTrackTransferActionSpec(
            sameTrack,
            4,
            true,
            GUARD_MS
        );

    assert(canExecute(action.tap));
    assert(!canExecute(action.hold));
    assert(action.hold.reason == ContextActionReason::SAME_SOURCE_TARGET);

    auto pending = sameTrack;
    pending.reason = ClipboardTransferReason::PASTE_PENDING;
    const auto pendingAction =
        core::state::sequencer::buildSequencerTrackTransferActionSpec(
            pending,
            4,
            false,
            GUARD_MS
        );
    assert(!canExecute(pendingAction.tap));
    assert(pendingAction.tap.reason == ContextActionReason::NO_ACTION);
    assert(!canExecute(pendingAction.hold));
    assert(pendingAction.hold.reason == ContextActionReason::PENDING);

    std::cout << "[PASS] test_blocked_reasons_are_mapped_without_changing_copy\n";
}

void test_paste_pending_view_model_and_semantic_projection_are_explicit() {
    auto pending = basePlan();
    pending.availability = ClipboardTransferAvailability::DISABLED;
    pending.reason = ClipboardTransferReason::PASTE_PENDING;

    const auto viewModel =
        core::ui::sequencer::buildSequencerTrackPastePendingViewModel(pending);
    assert(viewModel.visible);
    assert(viewModel.label != nullptr);
    assert(std::strcmp(viewModel.label, "Paste pending") == 0);

    const auto action =
        core::state::sequencer::buildSequencerTrackTransferActionSpec(
            pending,
            4,
            true,
            GUARD_MS
        );
    assert(!canExecute(action.hold));
    assert(action.hold.reason == ContextActionReason::PENDING);
    assert(std::strcmp(
        core::validation::ux::sequencerTrackTransferSemanticReason(
            action.hold.reason
        ),
        "paste_pending"
    ) == 0);

    const auto readyViewModel =
        core::ui::sequencer::buildSequencerTrackPastePendingViewModel(basePlan());
    assert(!readyViewModel.visible);
    assert(readyViewModel.label == nullptr);

    std::cout
        << "[PASS] test_paste_pending_view_model_and_semantic_projection_are_explicit\n";
}

}  // namespace

int main() {
    test_free_target_uses_copy_at_rest_and_guarded_green_paste();
    test_overwrite_and_no_route_are_explicit_amber_impacts();
    test_blocked_reasons_are_mapped_without_changing_copy();
    test_paste_pending_view_model_and_semantic_projection_are_explicit();
    std::cout << "All SequencerTrackTransferAction tests passed\n";
    return 0;
}
