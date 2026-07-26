#include <cassert>
#include <iostream>
#include <utility>

#include "state/StructureClipboardPastePlan.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
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

void test_single_track_plan_exposes_source_and_destination_owned_bindings() {
    core::state::StructureClipboardState clipboard;
    storeSingleTrackClipboard(clipboard, 0);
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::project::ProjectTrackState projectTracks;
    tracks.reset();
    tracks.syncSharedTrackState(0x0011, 0);
    assert(core::state::project::setProjectTrackMidiChannel(
        projectTracks, 4, 10
    ).changed());
    assert(core::state::project::setProjectTrackMuted(
        projectTracks, 4, true
    ).changed());

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        projectTracks,
        4
    );

    assert(clipboard.sequencerTrackSource == 0);
    assert(plan.payloadKind == core::state::StructureClipboardKind::SEQUENCER_TRACK);
    assert(plan.clipboardRevision == clipboard.revision.get());
    assert(plan.availability == ClipboardTransferAvailability::READY);
    assert(plan.reason == ClipboardTransferReason::NONE);
    assert(plan.canCommit());
    assert(plan.hasEntry);
    assert(plan.sourceMask == 0x0001);
    assert(plan.targetMask == 0x0010);
    assert(plan.createMask == 0);
    assert(plan.overwriteMask == 0x0010);
    assert(plan.entry.sourceTrack == 0);
    assert(plan.entry.targetTrack == 4);
    assert(plan.entry.targetMidiChannel == 10);
    assert(plan.entry.targetRouteValid);
    assert(plan.entry.targetMuted);
    assert(plan.entry.targetKind == ClipboardTransferTargetKind::OVERWRITE);
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
    core::state::project::ProjectTrackState projectTracks;
    tracks.reset();
    assert(core::state::project::setProjectTrackMidiChannel(
        projectTracks, 3, 8
    ).changed());

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        projectTracks,
        3
    );

    assert(plan.canCommit());
    assert(plan.createMask == 0x0008);
    assert(plan.overwriteMask == 0);
    assert(plan.entry.targetMidiChannel == 8);
    assert(!plan.entry.targetMuted);
    assert(plan.entry.targetKind == ClipboardTransferTargetKind::FREE);

    std::cout << "[PASS] test_single_track_plan_uses_free_slot_dormant_route\n";
}

void test_track_plan_reads_only_canonical_destination_route() {
    core::state::StructureClipboardState clipboard;
    storeSingleTrackClipboard(clipboard, 0);
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    core::state::project::ProjectTrackState projectTracks;
    tracks.reset();
    tracks.syncSharedTrackState(0x0003, 1);
    assert(core::state::project::setProjectTrackMidiChannel(
        projectTracks, 1, 11
    ).changed());

    const auto plan = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        projectTracks,
        1,
        0
    );

    assert(plan.canCommit());
    assert(plan.entry.targetTrack == 1);
    assert(plan.entry.targetMidiChannel == 11);
    assert(plan.entry.targetRouteValid);

    std::cout
        << "[PASS] test_track_plan_reads_only_canonical_destination_route\n";
}

void test_track_plan_reports_same_target_pending_invalid_and_missing_route() {
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::project::ProjectTrackState projectTracks;
    tracks.reset();

    core::state::StructureClipboardState sameClipboard;
    storeSingleTrackClipboard(sameClipboard, 0);
    const auto same = core::state::buildSequencerTrackClipboardTransferPlan(
        sameClipboard,
        tracks,
        projectTracks,
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
        projectTracks,
        4,
        0x0010
    );
    assert(pending.availability == ClipboardTransferAvailability::DISABLED);
    assert(pending.reason == ClipboardTransferReason::PASTE_PENDING);
    assert(!pending.canCommit());

    projectTracks.authored.midiChannels[4] = 0xFF;
    core::state::StructureClipboardState noRouteClipboard;
    storeSingleTrackClipboard(noRouteClipboard, 0);
    const auto noRoute = core::state::buildSequencerTrackClipboardTransferPlan(
        noRouteClipboard,
        tracks,
        projectTracks,
        4
    );
    assert(noRoute.availability == ClipboardTransferAvailability::WARNING);
    assert(noRoute.reason == ClipboardTransferReason::NO_ROUTE);
    assert(noRoute.canCommit());
    assert(!noRoute.entry.targetRouteValid);

    std::cout
        << "[PASS] test_track_plan_reports_same_target_pending_invalid_and_missing_route\n";
}

void test_track_plan_identity_allows_only_live_route_refresh() {
    core::state::StructureClipboardState clipboard;
    storeSingleTrackClipboard(clipboard, 0);
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::project::ProjectTrackState projectTracks;
    tracks.reset();
    assert(core::state::project::setProjectTrackMidiChannel(
        projectTracks, 4, 3
    ).changed());
    const auto original = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        projectTracks,
        4
    );
    assert(original.canCommit());

    assert(core::state::project::setProjectTrackMidiChannel(
        projectTracks, 4, 8
    ).changed());
    const auto rerouted = core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        tracks,
        projectTracks,
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
    changedTarget.entry.targetTrack = 5;
    changedTarget.targetMask = 0x0020;
    assert(!core::state::sameSequencerTrackClipboardTransferIdentity(
        original,
        changedTarget
    ));
}

}  // namespace

int main() {
    test_single_track_plan_exposes_source_and_destination_owned_bindings();
    test_single_track_plan_uses_free_slot_dormant_route();
    test_track_plan_reads_only_canonical_destination_route();
    test_track_plan_reports_same_target_pending_invalid_and_missing_route();
    test_track_plan_identity_allows_only_live_route_refresh();
    return 0;
}
