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

ui::SequencerTrackPasteProjection projection(
    uint8_t count = 1,
    bool overwriteAll = false
) {
    ui::SequencerTrackPasteProjection out{};
    auto& plan = out.plan;
    plan.payloadKind = count == 1
        ? core::state::StructureClipboardKind::SEQUENCER_TRACK
        : core::state::StructureClipboardKind::SEQUENCER_TRACK_SELECTION;
    plan.sourceCount = count;
    plan.count = count;
    plan.firstSource = 0;
    plan.lastSource = static_cast<uint8_t>(count - 1U);
    plan.firstTarget = count == 16 ? 0 : 4;
    plan.lastTarget = static_cast<uint8_t>(plan.firstTarget + count - 1U);
    plan.targetEndExclusive = static_cast<uint16_t>(plan.firstTarget + count);
    plan.availability = core::state::ClipboardTransferAvailability::READY;
    plan.reason = core::state::ClipboardTransferReason::NONE;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t target = static_cast<uint8_t>(plan.firstTarget + i);
        const uint16_t sourceBit = static_cast<uint16_t>(1U << i);
        const uint16_t targetBit = static_cast<uint16_t>(1U << target);
        plan.entries[i] = {
            .clipboardIndex = i,
            .sourceTrack = i,
            .targetTrack = target,
            .targetMidiChannel = static_cast<uint8_t>(i % 16U),
            .targetRouteValid = true,
            .targetMuted = (i & 1U) != 0,
            .inheritedLaneCount = static_cast<uint8_t>(i == 1 ? 2 : 0),
            .pinnedLaneCount = static_cast<uint8_t>(i == 1 ? 1 : 0),
            .targetKind = overwriteAll || (i & 1U) != 0
                ? core::state::ClipboardTransferTargetKind::OVERWRITE
                : core::state::ClipboardTransferTargetKind::FREE,
        };
        plan.sourceMask = static_cast<uint16_t>(plan.sourceMask | sourceBit);
        plan.targetMask = static_cast<uint16_t>(plan.targetMask | targetBit);
        plan.inheritedLaneCount = static_cast<uint8_t>(
            plan.inheritedLaneCount + plan.entries[i].inheritedLaneCount
        );
        plan.pinnedLaneCount = static_cast<uint8_t>(
            plan.pinnedLaneCount + plan.entries[i].pinnedLaneCount
        );
        if (plan.entries[i].targetKind ==
            core::state::ClipboardTransferTargetKind::FREE) {
            plan.createMask = static_cast<uint16_t>(plan.createMask | targetBit);
        } else {
            plan.overwriteMask = static_cast<uint16_t>(
                plan.overwriteMask | targetBit
            );
        }
    }
    out.targetTrack = plan.firstTarget;
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

void test_ready_preflight_is_visible_before_commit() {
    const auto model = ui::buildSequencerTrackPastePreflightViewModel(
        projection(),
        false,
        Telemetry{}
    );
    assert(model.visible);
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::READY);
    assert(model.tone == ui::SequencerTrackPastePreflightTone::CONSTRUCTIVE);
    assert(std::strcmp(model.header.data(), "Track paste | 1 Track") == 0);
    assert(std::strstr(model.mapping.data(), "T1>T5/C1") != nullptr);
    assert(std::strstr(model.detail.data(), "Hold Paste") != nullptr);
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

void assertDetailMapping(
    uint8_t count,
    uint8_t focused,
    const char* expectedOrdinal,
    const char* expectedMapping
) {
    auto value = projection(count);
    feedback(value, contextual::OperationFeedbackStatus::PREVIEW);
    value.detailVisible = true;
    value.focusedIndex = focused;
    const auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(model.visible);
    assert(std::strstr(model.header.data(), expectedOrdinal) != nullptr);
    assert(std::strcmp(model.mapping.data(), expectedMapping) == 0);
    assert(model.mappingIndex == focused);
    assert(model.mappingCount == count);
    assert(model.sourceTrack == value.plan.entries[focused].sourceTrack);
    assert(model.targetTrack == value.plan.entries[focused].targetTrack);
    assert(model.targetRouteValid);
}

void test_details_cover_first_middle_last_for_1_2_16_without_truncation() {
    assertDetailMapping(1, 0, "1/1", "T1 -> T5 | Ch1");
    assertDetailMapping(2, 0, "1/2", "T1 -> T5 | Ch1");
    assertDetailMapping(2, 1, "2/2", "T2 -> T6 | Ch2");
    assertDetailMapping(16, 0, "1/16", "T1 -> T1 | Ch1");
    assertDetailMapping(16, 7, "8/16", "T8 -> T8 | Ch8");
    assertDetailMapping(16, 15, "16/16", "T16 -> T16 | Ch16");

    const auto summary = ui::buildSequencerTrackPastePreflightViewModel(
        projection(16),
        false,
        Telemetry{}
    );
    assert(std::strstr(summary.mapping.data(), "T1>T1/C1") != nullptr);
    assert(std::strstr(summary.mapping.data(), "T8>T8/C8") != nullptr);
    assert(std::strstr(summary.mapping.data(), "T16>T16/C16") != nullptr);
    assert(std::strlen(summary.mapping.data()) < summary.mapping.size() - 1U);
}

void test_detail_exposes_kind_mute_lane_and_live_route_semantics() {
    auto value = projection(3);
    feedback(value, contextual::OperationFeedbackStatus::PREVIEW);
    value.detailVisible = true;
    value.focusedIndex = 1;
    auto model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(std::strcmp(model.header.data(), "Track paste | 2/3") == 0);
    assert(std::strcmp(model.mapping.data(), "T2 -> T6 | Ch2") == 0);
    assert(std::strcmp(model.footprint.data(), "Overwrite | Mute kept") == 0);
    assert(std::strcmp(
        model.laneBindings.data(),
        "CC | 2 inherit target | 1 pinned"
    ) == 0);

    value.plan.entries[1].targetMidiChannel = 8;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        Telemetry{}
    );
    assert(std::strstr(model.mapping.data(), "Ch9") != nullptr);
    assert(model.targetMidiChannel == 8);
}

void test_exact_activation_generation_drives_mixed_then_applied() {
    auto value = projection(2, true);
    feedback(value, contextual::OperationFeedbackStatus::QUEUED);
    value.operationGeneration = 41;
    value.activationGeneration = 7;
    Telemetry telemetry{};
    telemetry[4] = {
        seq::SequencerTrackActivationStatus::APPLIED,
        7,
        seq::SequencerTrackActivationOrigin::TRACK_PASTE,
    };
    telemetry[5] = {
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

    telemetry[5].status = seq::SequencerTrackActivationStatus::APPLIED;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        telemetry
    );
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::APPLIED);
    assert(ui::shouldShowSequencerTrackPasteAppliedConfirmation(model, 0));
    assert(!ui::shouldShowSequencerTrackPasteAppliedConfirmation(model, 7));

    telemetry[4].generation = 8;
    telemetry[5].generation = 8;
    model = ui::buildSequencerTrackPastePreflightViewModel(
        value,
        false,
        telemetry
    );
    // A later operation must never be mistaken for generation 7.
    assert(model.phase == ui::SequencerTrackPastePreflightPhase::QUEUED);
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
    test_ready_preflight_is_visible_before_commit();
    test_guard_phases_explain_copy_cancel_and_progress();
    test_details_cover_first_middle_last_for_1_2_16_without_truncation();
    test_detail_exposes_kind_mute_lane_and_live_route_semantics();
    test_exact_activation_generation_drives_mixed_then_applied();
    test_blocked_reason_is_explicit();
    return 0;
}
