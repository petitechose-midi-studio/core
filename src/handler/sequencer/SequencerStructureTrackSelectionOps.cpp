#include "handler/sequencer/SequencerStructureTrackSelectionOps.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
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

FLASHMEM uint16_t activeTrackSelectionMask(
    uint16_t selectedMask,
    uint16_t enabledMask
) {
    return static_cast<uint16_t>(selectedMask & enabledMask);
}

FLASHMEM bool toggleSelectedSequencerStructureTrackMute(
    core::state::sequencer::SequencerTrackBankState& tracks,
    uint16_t selectedMask
) {
    bool anyAudible = false;
    for (uint8_t track = 0;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const uint16_t bit = structure_slots::slotBit(track);
        if ((selectedMask & bit) == 0) continue;
        anyAudible = anyAudible || !tracks.isTrackMuted(track);
    }

    bool changed = false;
    for (uint8_t track = 0;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const uint16_t bit = structure_slots::slotBit(track);
        if ((selectedMask & bit) == 0) continue;
        changed = tracks.setTrackMuted(track, anyAudible) || changed;
    }
    return changed;
}

FLASHMEM core::state::shared::MaskMutation removeSelectedSequencerStructureTracks(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t activeTrack
) {
    return structure_slots::removeSelected(
        enabledMask,
        activeTrackSelectionMask(selectedMask, enabledMask),
        activeTrack,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
}

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

    if (!core::state::sequencer::storeActiveTrack(tracks, sequencer)) return nullptr;
    for (uint8_t track = firstTrack;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if ((selectedMask & structure_slots::slotBit(track)) == 0) continue;
        if (clipboard->count >= clipboard->tracks.size()) break;

        auto& entry = clipboard->tracks[clipboard->count++];
        entry.valid = true;
        entry.sourceTrack = track;
        core::state::sequencer::captureSnapshot(tracks.track(track), entry.snapshot);
        if (!core::state::cloneSequencerGraph(
                entry.graph,
                core::state::sequencer::graphView(tracks.track(track))
            ) ||
            !core::state::sequencer::cloneSequencerCcLaneBank(
                entry.ccLanes,
                core::state::sequencer::sequencerCcLaneView(tracks.track(track))
            )) {
            return nullptr;
        }
    }

    return clipboard->count == 0 ? nullptr : std::move(clipboard);
}

}  // namespace core::handler
