#include "handler/sequencer/SequencerStructureSelectionOps.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

FLASHMEM uint8_t firstSelectedSlot(
    uint16_t mask,
    uint8_t limit
) {
    for (uint8_t index = 0; index < limit; ++index) {
        if ((mask & structure_slots::slotBit(index)) != 0U) {
            return index;
        }
    }
    return limit;
}

}  // namespace

FLASHMEM uint16_t activeTrackSelectionMask(
    uint16_t selectedMask,
    uint16_t enabledMask
) {
    return static_cast<uint16_t>(selectedMask & enabledMask);
}

FLASHMEM core::state::shared::MaskMutation deleteSelectedStructureTracks(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t activeTrack
) {
    const uint16_t deleteMask =
        activeTrackSelectionMask(selectedMask, enabledMask);
    const uint8_t trackCount =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    const uint8_t enabledCount =
        structure_slots::countEnabled(enabledMask, trackCount);
    const uint8_t deleteCount =
        structure_slots::countEnabled(deleteMask, trackCount);
    if (deleteCount == 0U || deleteCount >= enabledCount) {
        return {
            .nextMask = enabledMask,
            .nextActive = activeTrack,
            .changed = false,
        };
    }

    const uint16_t nextMask = static_cast<uint16_t>(
        enabledMask & static_cast<uint16_t>(~deleteMask)
    );
    const uint8_t nextActive = structure_slots::isEnabled(
        nextMask,
        activeTrack
    )
        ? activeTrack
        : structure_slots::nextEnabledIndex(
              nextMask,
              activeTrack,
              trackCount
          );
    return {
        .nextMask = nextMask,
        .nextActive = nextActive,
        .changed = true,
    };
}

FLASHMEM uint16_t activeContentPageSelectionMask(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
) {
    return static_cast<uint16_t>(
        selectedMask &
        structure_slots::prefixMask(
            core::state::sequencer::activeContentPageCount(sequencer)
        )
    );
}

FLASHMEM core::app::ExtmemUniquePtr<
    core::state::SequencerTrackSelectionClipboard
> captureTrackSelectionClipboard(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::macro::MacroPagesState& pages,
    uint16_t selectedMask
) {
    using TrackBank =
        core::state::sequencer::SequencerTrackBankState;
    const uint16_t mask = activeTrackSelectionMask(
        selectedMask,
        tracks.currentEnabledMask()
    );
    const uint8_t firstTrack = firstSelectedSlot(
        mask,
        TrackBank::TRACK_COUNT
    );
    if (firstTrack >= TrackBank::TRACK_COUNT) return nullptr;

    auto clipboard = core::app::makeExtmemUnique<
        core::state::SequencerTrackSelectionClipboard
    >();
    if (!clipboard) return nullptr;
    clipboard->projectControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >(pages.control.authored);
    if (!clipboard->projectControl) return nullptr;
    clipboard->valid = true;

    if (!core::state::sequencer::storeActiveTrack(tracks, sequencer)) {
        return nullptr;
    }
    for (uint8_t track = firstTrack;
         track < TrackBank::TRACK_COUNT;
         ++track) {
        if ((mask & structure_slots::slotBit(track)) == 0U) continue;
        if (clipboard->count >= clipboard->tracks.size()) return nullptr;

        auto& entry = clipboard->tracks[clipboard->count++];
        entry.valid = true;
        entry.sourceTrack = track;
        entry.macroTrack = pages.tracks[track];
        core::state::sequencer::captureSnapshot(
            tracks.track(track),
            entry.snapshot
        );
        if (!core::state::cloneSequencerGraph(
                entry.graph,
                core::state::sequencer::graphView(
                    tracks.track(track)
                )
            ) ||
            !core::state::sequencer::cloneSequencerCcLaneBank(
                entry.ccLanes,
                core::state::sequencer::sequencerCcLaneView(
                    tracks.track(track)
                )
            )) {
            return nullptr;
        }
        if (tracks.isDrumTrack(track)) {
            entry.drumTrack = core::app::makeExtmemUnique<
                core::state::sequencer::DrumTrackState
            >(tracks.drumTrack(track));
            if (!entry.drumTrack) return nullptr;
        }
    }

    return clipboard->count == 0U
        ? nullptr
        : std::move(clipboard);
}

FLASHMEM bool capturePageSelectionClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask,
    core::state::SequencerPageSelectionClipboard& clipboard
) {
    using Pattern =
        core::state::sequencer::SequencerPatternState;
    if (!core::state::sequencer::isRootContentView(sequencer)) {
        return false;
    }

    const uint16_t mask = activeContentPageSelectionMask(
        sequencer,
        selectedMask
    );
    const uint8_t firstPage =
        firstSelectedSlot(mask, Pattern::PAGE_COUNT);
    if (firstPage >= Pattern::PAGE_COUNT) return false;

    clipboard.reset();
    clipboard.valid = true;
    clipboard.sourceFirstPage = firstPage;
    for (uint8_t page = firstPage;
         page < Pattern::PAGE_COUNT;
         ++page) {
        if ((mask & structure_slots::slotBit(page)) == 0U) continue;
        if (clipboard.count >= clipboard.pages.size()) return false;
        auto& entry = clipboard.pages[clipboard.count];
        if (!capturePageClipboard(sequencer, page, entry)) {
            return false;
        }
        ++clipboard.count;
    }
    return clipboard.count > 0U;
}

FLASHMEM core::state::SequencerPageSelectionPastePlan
buildPageSelectionPastePlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    uint8_t cursorPage
) {
    if (!core::state::sequencer::isRootContentView(sequencer) ||
        !structureClipboard.hasSequencerPageSelection()) {
        return {};
    }
    return core::state::buildSequencerPageSelectionPastePlan(
        structureClipboard.sequencerPageSelection,
        cursorPage,
        sequencer.activePageCount()
    );
}

}  // namespace core::handler
