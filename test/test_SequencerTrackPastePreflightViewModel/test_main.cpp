#include <array>
#include <cassert>
#include <cstring>

#include "state/sequencer/SequencerTrackTransferAction.hpp"
#include "ui/sequencer/SequencerTrackPastePreflightViewModel.hpp"

namespace {

namespace contextual = core::state::contextual;
namespace seq = core::state::sequencer;
namespace ui = core::ui::sequencer;

using Telemetry = std::array<
    seq::SequencerTrackActivationTelemetry,
    seq::SequencerTrackBankState::TRACK_COUNT>;

ui::SequencerTrackPasteProjection projection(bool overwrite = false) {
    ui::SequencerTrackPasteProjection out{};
    auto& plan = out.plan;
    plan.payloadKind = core::state::StructureClipboardKind::SEQUENCER_TRACK;
    plan.sourceMask = 0x0001;
    plan.targetMask = 0x0010;
    plan.createMask = overwrite ? 0 : 0x0010;
    plan.overwriteMask = overwrite ? 0x0010 : 0;
    plan.availability = core::state::ClipboardTransferAvailability::READY;
    plan.reason = core::state::ClipboardTransferReason::NONE;
    plan.sourceCount = 1;
    plan.count = 1;
    plan.firstSource = 0;
    plan.lastSource = 0;
    plan.firstTarget = 4;
    plan.lastTarget = 4;
    plan.entries[0] = {
        .sourceTrack = 0,
        .targetTrack = 4,
        .targetMidiChannel = 0,
        .targetRouteValid = true,
        .targetMuted = false,
        .inheritedLaneCount = 0,
        .pinnedLaneCount = 0,
        .targetKind = overwrite
            ? core::state::ClipboardTransferTargetKind::OVERWRITE
            : core::state::ClipboardTransferTargetKind::FREE,
    };
    out.targetTrack = plan.entries[0].targetTrack;
    out.copyAvailable = true;
    out.action = seq::buildSequencerTrackTransferActionSpec(
        plan,
        out.targetTrack,
        true,
        1000
    );
    return out;
}
void feedback(
    ui::SequencerTrackPasteProjection& value,
    contextual::OperationFeedbackStatus status
) {
    value.feedback.active = status != contextual::OperationFeedbackStatus::NONE;
    value.feedback.action = contextual::ContextActionId::PASTE;
    value.feedback.status = status;
}

void test_ready_preflight_is_quiet_until_details_are_requested() {
    auto value = projection();
    auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(!model.visible);
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::READY);

    value.detailVisible = true;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(model.visible);
    assert(model.tone == ui::SequencerTrackPastePreflightTone::CONSTRUCTIVE);
    assert(std::strcmp(model.header.data(), "Track paste | 1/1") == 0);
    assert(std::strcmp(model.mapping.data(), "T1 -> T5 | Ch1") == 0);
}

void test_guard_phases_explain_copy_cancel_and_progress() {
    auto value = projection();
    feedback(value, contextual::OperationFeedbackStatus::PRESSED);
    auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::HOLDING);
    assert(std::strstr(model.detail.data(), "Copy") != nullptr);

    feedback(value, contextual::OperationFeedbackStatus::ARMED);
    value.guard.phase = contextual::GuardedActionPhase::ARMED;
    value.guard.progressPermille = 570;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(std::strstr(model.detail.data(), "57%") != nullptr);
    assert(std::strstr(model.detail.data(), "release cancels") != nullptr);

    feedback(value, contextual::OperationFeedbackStatus::CANCELLED);
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::CANCELLED);
    assert(std::strstr(model.detail.data(), "no changes") != nullptr);
}

void test_detail_exposes_kind_mute_lane_and_live_route_semantics() {
    auto value = projection(true);
    value.plan.entries[0].targetMuted = true;
    value.plan.entries[0].inheritedLaneCount = 2;
    value.plan.entries[0].pinnedLaneCount = 1;
    value.plan.entries[0].targetMidiChannel = 1;
    feedback(value, contextual::OperationFeedbackStatus::PREVIEW);
    value.detailVisible = true;
    auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(std::strcmp(model.header.data(), "Track paste | 1/1") == 0);
    assert(std::strcmp(model.mapping.data(), "T1 -> T5 | Ch2") == 0);
    assert(std::strcmp(model.footprint.data(), "Overwrite | Mute kept") == 0);
    assert(std::strcmp(
        model.laneBindings.data(),
        "CC | 2 inherit target | 1 pinned"
    ) == 0);

    value.plan.entries[0].targetMidiChannel = 8;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(std::strstr(model.mapping.data(), "Ch9") != nullptr);
    assert(model.targetMidiChannel == 8);
}
void test_exact_activation_generation_drives_queued_then_applied() {
    auto value = projection(true);
    feedback(value, contextual::OperationFeedbackStatus::QUEUED);
    value.operationGeneration = 41;
    value.activationGeneration = 7;
    Telemetry telemetry{};
    telemetry[4] = {
        seq::SequencerTrackActivationStatus::QUEUED,
        7,
        seq::SequencerTrackActivationOrigin::TRACK_PASTE,
    };
    auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        telemetry
    );
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::QUEUED);
    assert(model.activationGeneration == 7);
    assert(model.operationGeneration == 41);

    telemetry[4].status = seq::SequencerTrackActivationStatus::APPLIED;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        telemetry
    );
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::APPLIED);
    assert(ui::shouldShowSequencerTrackPasteAppliedConfirmation(model, 0));
    assert(!ui::shouldShowSequencerTrackPasteAppliedConfirmation(model, 41));

    telemetry[4].generation = 8;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        telemetry
    );
    // A later operation must never be mistaken for generation 7.
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::QUEUED);
}
void test_immediate_apply_uses_operation_identity_without_activation_generation() {
    auto value = projection();
    feedback(value, contextual::OperationFeedbackStatus::APPLIED);
    value.operationGeneration = 84;
    value.activationGeneration = 0;
    const auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(model.visible);
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::APPLIED);
    assert(ui::shouldShowSequencerTrackPasteAppliedConfirmation(model, 0));
    assert(!ui::shouldShowSequencerTrackPasteAppliedConfirmation(model, 84));
}

void test_blocked_reason_is_explicit() {
    auto value = projection();
    value.plan.availability = core::state::ClipboardTransferAvailability::DISABLED;
    value.plan.reason = core::state::ClipboardTransferReason::SAME_TRACK;
    feedback(value, contextual::OperationFeedbackStatus::BLOCKED);
    const auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(model.visible);
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::BLOCKED);
    assert(model.tone == ui::SequencerTrackPastePreflightTone::ERROR);
    assert(std::strcmp(model.detail.data(), "Same Track") == 0);
}

}  // namespace

int main() {
    test_ready_preflight_is_quiet_until_details_are_requested();
    test_guard_phases_explain_copy_cancel_and_progress();
    test_detail_exposes_kind_mute_lane_and_live_route_semantics();
    test_exact_activation_generation_drives_queued_then_applied();
    test_immediate_apply_uses_operation_identity_without_activation_generation();
    test_blocked_reason_is_explicit();
    return 0;
}
