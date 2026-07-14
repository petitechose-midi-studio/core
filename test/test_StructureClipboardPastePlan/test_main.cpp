#include <cassert>
#include <iostream>
#include <utility>

#include "state/StructureClipboardPastePlan.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace {

using core::state::ClipboardTransferAvailability;
using core::state::ClipboardTransferReason;
using core::state::ClipboardTransferTargetKind;

void storeSingleTrackClipboard(
    core::state::StructureClipboardState& clipboard,
    uint8_t sourceTrack
) {
    core::state::sequencer::SequencerPatternSnapshot snapshot;
    assert(clipboard.storeSequencerTrack(snapshot, nullptr, sourceTrack));
}

void storeTrackSelectionClipboard(
    core::state::StructureClipboardState& clipboard,
    std::initializer_list<uint8_t> sourceTracks
) {
    auto selection = core::app::makeExtmemUnique<
        core::state::SequencerTrackSelectionClipboard
    >();
    assert(selection != nullptr);
    selection->valid = true;
    for (const uint8_t sourceTrack : sourceTracks) {
        assert(selection->count < selection->tracks.size());
        auto& entry = selection->tracks[selection->count++];
        entry.valid = true;
        entry.sourceTrack = sourceTrack;
    }
    assert(clipboard.storeSequencerTrackSelection(std::move(selection)));
}

void test_single_track_plan_exposes_source_and_destination_owned_bindings() {
    core::state::StructureClipboardState clipboard;
    storeSingleTrackClipboard(clipboard, 0);
    core::state::sequencer::SequencerTrackBankState tracks;
    tracks.reset();
    tracks.syncSharedTrackState(0x0011, 0);
    tracks.track(4).midiChannel.set(10);
    assert(tracks.setTrackMuted(4, true));

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        4
    );

    assert(clipboard.sequencerTrackSource == 0);
    assert(plan.payloadKind == core::state::StructureClipboardKind::SEQUENCER_TRACK);
    assert(plan.clipboardRevision == clipboard.revision.get());
    assert(plan.availability == ClipboardTransferAvailability::READY);
    assert(plan.reason == ClipboardTransferReason::NONE);
    assert(plan.canCommit());
    assert(plan.sourceCount == 1);
    assert(plan.count == 1);
    assert(plan.sourceMask == 0x0001);
    assert(plan.targetMask == 0x0010);
    assert(plan.createMask == 0);
    assert(plan.overwriteMask == 0x0010);
    assert(plan.firstSource == 0);
    assert(plan.lastSource == 0);
    assert(plan.firstTarget == 4);
    assert(plan.lastTarget == 4);
    assert(plan.entries[0].sourceTrack == 0);
    assert(plan.entries[0].targetTrack == 4);
    assert(plan.entries[0].targetMidiChannel == 10);
    assert(plan.entries[0].targetRouteValid);
    assert(plan.entries[0].targetMuted);
    assert(plan.entries[0].targetKind == ClipboardTransferTargetKind::OVERWRITE);
    assert((plan.bindingPolicy & core::state::CLIPBOARD_TRANSFER_PRESERVE_ROUTE) != 0);
    assert((plan.bindingPolicy & core::state::CLIPBOARD_TRANSFER_PRESERVE_MUTE) != 0);
    assert((plan.bindingPolicy & core::state::CLIPBOARD_TRANSFER_PRESERVE_SLOT) != 0);

    clipboard.clear();
    assert(clipboard.sequencerTrackSource ==
           core::state::sequencer::SequencerTrackBankState::TRACK_COUNT);

    std::cout
        << "[PASS] test_single_track_plan_exposes_source_and_destination_owned_bindings\n";
}

void test_single_track_plan_uses_free_slot_dormant_route() {
    core::state::StructureClipboardState clipboard;
    storeSingleTrackClipboard(clipboard, 0);
    core::state::sequencer::SequencerTrackBankState tracks;
    tracks.reset();
    tracks.track(3).midiChannel.set(8);

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        3
    );

    assert(plan.canCommit());
    assert(plan.createMask == 0x0008);
    assert(plan.overwriteMask == 0);
    assert(plan.entries[0].targetMidiChannel == 8);
    assert(!plan.entries[0].targetMuted);
    assert(plan.entries[0].targetKind == ClipboardTransferTargetKind::FREE);

    std::cout << "[PASS] test_single_track_plan_uses_free_slot_dormant_route\n";
}

void test_track_plan_reads_active_destination_route_from_live_editor() {
    core::state::StructureClipboardState clipboard;
    storeSingleTrackClipboard(clipboard, 0);
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    tracks.reset();
    tracks.syncSharedTrackState(0x0003, 1);
    tracks.track(1).midiChannel.set(3);
    editor.pattern.midiChannel.set(11);

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        1,
        0,
        &editor
    );

    assert(plan.canCommit());
    assert(plan.entries[0].targetTrack == 1);
    assert(plan.entries[0].targetMidiChannel == 11);
    assert(plan.entries[0].targetRouteValid);

    std::cout
        << "[PASS] test_track_plan_reads_active_destination_route_from_live_editor\n";
}

void test_track_selection_plan_collapses_source_gaps_into_contiguous_targets() {
    core::state::StructureClipboardState clipboard;
    storeTrackSelectionClipboard(clipboard, {0, 2});
    core::state::sequencer::SequencerTrackBankState tracks;
    tracks.reset();
    tracks.syncSharedTrackState(0x0011, 0);
    tracks.track(4).midiChannel.set(9);
    tracks.track(5).midiChannel.set(12);

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        4
    );

    assert(plan.canCommit());
    assert(plan.sourceCount == 2);
    assert(plan.count == 2);
    assert(plan.sourceMask == 0x0005);
    assert(plan.targetMask == 0x0030);
    assert(plan.overwriteMask == 0x0010);
    assert(plan.createMask == 0x0020);
    assert(plan.firstSource == 0);
    assert(plan.lastSource == 2);
    assert(plan.firstTarget == 4);
    assert(plan.lastTarget == 5);
    assert(plan.entries[0].sourceTrack == 0);
    assert(plan.entries[0].targetTrack == 4);
    assert(plan.entries[0].targetMidiChannel == 9);
    assert(plan.entries[1].sourceTrack == 2);
    assert(plan.entries[1].targetTrack == 5);
    assert(plan.entries[1].targetMidiChannel == 12);

    std::cout
        << "[PASS] test_track_selection_plan_collapses_source_gaps_into_contiguous_targets\n";
}

void test_track_selection_plan_rejects_out_of_range_without_partial_projection() {
    core::state::StructureClipboardState clipboard;
    storeTrackSelectionClipboard(clipboard, {0, 2});
    core::state::sequencer::SequencerTrackBankState tracks;
    tracks.reset();

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        15
    );

    assert(plan.availability == ClipboardTransferAvailability::DISABLED);
    assert(plan.reason == ClipboardTransferReason::OUT_OF_RANGE);
    assert(!plan.canCommit());
    assert(plan.sourceCount == 2);
    assert(plan.count == 0);
    assert(plan.targetMask == 0);
    assert(plan.createMask == 0);
    assert(plan.overwriteMask == 0);
    assert(plan.targetEndExclusive == 17);

    std::cout
        << "[PASS] test_track_selection_plan_rejects_out_of_range_without_partial_projection\n";
}

void test_track_plan_reports_same_target_pending_invalid_and_missing_route() {
    core::state::sequencer::SequencerTrackBankState tracks;
    tracks.reset();

    core::state::StructureClipboardState sameClipboard;
    storeSingleTrackClipboard(sameClipboard, 0);
    const auto same = core::state::buildSequencerTrackClipboardTransferPlan(
        sameClipboard,
        tracks,
        0
    );
    assert(same.availability == ClipboardTransferAvailability::DISABLED);
    assert(same.reason == ClipboardTransferReason::SAME_TRACK);
    assert(!same.canCommit());

    core::state::StructureClipboardState pendingClipboard;
    storeSingleTrackClipboard(pendingClipboard, 0);
    const auto pending = core::state::buildSequencerTrackClipboardTransferPlan(
        pendingClipboard,
        tracks,
        4,
        0x0010
    );
    assert(pending.availability == ClipboardTransferAvailability::DISABLED);
    assert(pending.reason == ClipboardTransferReason::PASTE_PENDING);
    assert(!pending.canCommit());

    tracks.track(4).midiChannel.set(0xFF);
    core::state::StructureClipboardState noRouteClipboard;
    storeSingleTrackClipboard(noRouteClipboard, 0);
    const auto noRoute = core::state::buildSequencerTrackClipboardTransferPlan(
        noRouteClipboard,
        tracks,
        4
    );
    assert(noRoute.availability == ClipboardTransferAvailability::WARNING);
    assert(noRoute.reason == ClipboardTransferReason::NO_ROUTE);
    assert(noRoute.canCommit());
    assert(!noRoute.entries[0].targetRouteValid);

    auto corruptSelection = core::app::makeExtmemUnique<
        core::state::SequencerTrackSelectionClipboard
    >();
    assert(corruptSelection != nullptr);
    corruptSelection->valid = true;
    corruptSelection->count = static_cast<uint8_t>(corruptSelection->tracks.size() + 1U);
    core::state::StructureClipboardState corruptClipboard;
    assert(corruptClipboard.storeSequencerTrackSelection(std::move(corruptSelection)));
    const auto invalid = core::state::buildSequencerTrackClipboardTransferPlan(
        corruptClipboard,
        tracks,
        1
    );
    assert(invalid.availability == ClipboardTransferAvailability::DISABLED);
    assert(invalid.reason == ClipboardTransferReason::INVALID_PAYLOAD);
    assert(!invalid.canCommit());

    std::cout
        << "[PASS] test_track_plan_reports_same_target_pending_invalid_and_missing_route\n";
}

void test_track_selection_plan_allows_mixed_identity_mapping() {
    core::state::StructureClipboardState clipboard;
    storeTrackSelectionClipboard(clipboard, {0, 2});
    core::state::sequencer::SequencerTrackBankState tracks;
    tracks.reset();

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        0
    );

    assert(plan.canCommit());
    assert(plan.entries[0].sourceTrack == plan.entries[0].targetTrack);
    assert(plan.entries[1].sourceTrack == 2);
    assert(plan.entries[1].targetTrack == 1);

    std::cout << "[PASS] test_track_selection_plan_allows_mixed_identity_mapping\n";
}

void test_track_plan_identity_allows_only_live_route_refresh() {
    core::state::StructureClipboardState clipboard;
    storeSingleTrackClipboard(clipboard, 0);
    core::state::sequencer::SequencerTrackBankState tracks;
    tracks.reset();
    tracks.track(4).midiChannel.set(3);
    const auto original = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        4
    );
    assert(original.canCommit());

    tracks.track(4).midiChannel.set(8);
    const auto rerouted = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        4
    );
    assert(core::state::sameSequencerTrackClipboardTransferIdentity(
        original,
        rerouted
    ));
    assert(!core::state::sameSequencerTrackClipboardTransferPlan(
        original,
        rerouted
    ));

    auto changedTarget = rerouted;
    changedTarget.entries[0].targetTrack = 5;
    changedTarget.targetMask = 0x0020;
    assert(!core::state::sameSequencerTrackClipboardTransferIdentity(
        original,
        changedTarget
    ));
}

void test_page_selection_paste_plan_projects_offsets_and_overwrite() {
    core::state::SequencerPageSelectionClipboard clipboard;
    clipboard.valid = true;
    clipboard.sourceFirstPage = 1;
    clipboard.count = 2;
    clipboard.pages[0].valid = true;
    clipboard.pages[0].sourcePage = 1;
    clipboard.pages[1].valid = true;
    clipboard.pages[1].sourcePage = 3;

    const auto plan = core::state::buildSequencerPageSelectionPastePlan(
        clipboard,
        4,
        6
    );

    assert(plan.hasEntries());
    assert(plan.count == 2);
    assert(plan.firstDestinationPage == 4);
    assert(plan.entries[0].clipboardIndex == 0);
    assert(plan.entries[0].destinationPage == 4);
    assert(plan.entries[1].clipboardIndex == 1);
    assert(plan.entries[1].destinationPage == 6);
    assert(plan.destinationMask == ((1U << 4) | (1U << 6)));
    assert(plan.overwriteMask == (1U << 4));

    std::cout << "[PASS] test_page_selection_paste_plan_projects_offsets_and_overwrite\n";
}

void test_page_selection_paste_plan_clips_after_page_limit() {
    core::state::SequencerPageSelectionClipboard clipboard;
    clipboard.valid = true;
    clipboard.sourceFirstPage = 0;
    clipboard.count = 2;
    clipboard.pages[0].valid = true;
    clipboard.pages[0].sourcePage = 0;
    clipboard.pages[1].valid = true;
    clipboard.pages[1].sourcePage = 2;

    const auto plan = core::state::buildSequencerPageSelectionPastePlan(
        clipboard,
        15,
        16
    );

    assert(plan.hasEntries());
    assert(plan.count == 1);
    assert(plan.firstDestinationPage == 15);
    assert(plan.entries[0].clipboardIndex == 0);
    assert(plan.entries[0].destinationPage == 15);
    assert(plan.destinationMask == (1U << 15));
    assert(plan.overwriteMask == (1U << 15));

    std::cout << "[PASS] test_page_selection_paste_plan_clips_after_page_limit\n";
}

}  // namespace

int main() {
    test_single_track_plan_exposes_source_and_destination_owned_bindings();
    test_single_track_plan_uses_free_slot_dormant_route();
    test_track_plan_reads_active_destination_route_from_live_editor();
    test_track_selection_plan_collapses_source_gaps_into_contiguous_targets();
    test_track_selection_plan_rejects_out_of_range_without_partial_projection();
    test_track_plan_reports_same_target_pending_invalid_and_missing_route();
    test_track_selection_plan_allows_mixed_identity_mapping();
    test_track_plan_identity_allows_only_live_route_refresh();
    test_page_selection_paste_plan_projects_offsets_and_overwrite();
    test_page_selection_paste_plan_clips_after_page_limit();
    return 0;
}
