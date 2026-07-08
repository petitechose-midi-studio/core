#include "handler/sequencer/SequencerStructureTrackSelectionOps.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

FLASHMEM uint8_t firstSelectedTrack(uint16_t mask) {
    for (uint8_t track = 0;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if ((mask & structure_slots::slotBit(track)) != 0) return track;
    }
    return core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
}

}  // namespace

FLASHMEM core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard>
captureTrackSelectionClipboard(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
) {
    const uint8_t firstTrack = firstSelectedTrack(selectedMask);
    if (firstTrack >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
        return nullptr;
    }

    auto clipboard = core::app::makeExtmemUnique<
        core::state::SequencerTrackSelectionClipboard
    >();
    if (!clipboard) return nullptr;
    clipboard->valid = true;

    core::state::sequencer::storeActiveTrack(tracks, sequencer);
    for (uint8_t track = firstTrack;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if ((selectedMask & structure_slots::slotBit(track)) == 0) continue;
        if (clipboard->count >= clipboard->tracks.size()) break;

        auto& entry = clipboard->tracks[clipboard->count++];
        entry.valid = true;
        entry.offset = static_cast<uint8_t>(track - firstTrack);
        core::state::sequencer::captureSnapshot(tracks.track(track), entry.snapshot);
        if (!core::state::cloneSequencerGraph(
                entry.graph,
                core::state::sequencer::graphView(tracks.track(track))
            )) {
            return nullptr;
        }
    }

    return clipboard->count == 0 ? nullptr : std::move(clipboard);
}

FLASHMEM SequencerTrackSelectionPasteTargets buildTrackSelectionPasteTargets(
    const core::state::SequencerTrackSelectionClipboard& clipboard,
    uint8_t cursorTrack
) {
    SequencerTrackSelectionPasteTargets targets;
    const uint8_t cursor =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(cursorTrack);
    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const auto& entry = clipboard.tracks[i];
        if (!entry.valid) continue;
        const uint16_t target = static_cast<uint16_t>(cursor) + entry.offset;
        if (target >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) continue;

        const uint8_t targetTrack = static_cast<uint8_t>(target);
        targets.targetMask = static_cast<uint16_t>(
            targets.targetMask | structure_slots::slotBit(targetTrack)
        );
        targets.firstTarget = std::min(targets.firstTarget, targetTrack);
    }
    return targets;
}

FLASHMEM void pasteTrackSelectionClipboard(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerTrackSelectionClipboard& clipboard,
    uint8_t cursorTrack,
    uint8_t previousActiveTrack
) {
    const uint8_t cursor =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(cursorTrack);
    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const auto& entry = clipboard.tracks[i];
        if (!entry.valid) continue;
        const uint16_t target = static_cast<uint16_t>(cursor) + entry.offset;
        if (target >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) continue;

        const uint8_t targetTrack = static_cast<uint8_t>(target);
        core::state::sequencer::applySnapshot(tracks.track(targetTrack), entry.snapshot);
        core::state::sequencer::copyGraph(
            tracks.track(targetTrack),
            entry.graph.get(),
            entry.snapshot.graphRevision
        );
        if (targetTrack == previousActiveTrack) {
            core::state::sequencer::applySnapshotToEditor(sequencer, entry.snapshot);
            core::state::sequencer::copyGraph(
                sequencer.pattern,
                entry.graph.get(),
                entry.snapshot.graphRevision
            );
        }
    }
}

}  // namespace core::handler
