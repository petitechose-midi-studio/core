#include "handler/sequencer/SequencerStructureSelectionOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

FLASHMEM uint16_t activeTrackSelectionMask(
    uint16_t selectedMask,
    uint16_t enabledMask
) {
    return static_cast<uint16_t>(selectedMask & enabledMask);
}

FLASHMEM core::state::shared::MaskMutation removeSelectedStructureTracks(
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

FLASHMEM bool resetSelectedActiveContentPages(
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask,
    StepResetDepth depth
) {
    const uint16_t pageMask =
        activeContentPageSelectionMask(sequencer, selectedMask);
    if (pageMask == 0U) return false;

    oc::note::sequencer::StepBitMask128 stepMask{};
    const uint8_t activeLength =
        core::state::sequencer::activeContentLength(sequencer);
    for (uint16_t step = 0; step < activeLength; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        const uint8_t page =
            core::state::sequencer::activeContentPageForStep(stepIndex);
        if ((pageMask & structure_slots::slotBit(page)) != 0U) {
            stepMask.setBit(stepIndex, true);
        }
    }
    return resetSelectedActiveContentSteps(sequencer, stepMask, depth);
}

FLASHMEM bool removeSelectedRootPages(
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
) {
    if (!core::state::sequencer::isRootContentView(sequencer)) return false;

    const uint8_t pageCount = sequencer.activePageCount();
    const uint16_t pageMask = static_cast<uint16_t>(
        selectedMask & structure_slots::prefixMask(pageCount)
    );
    const uint8_t deleteCount =
        structure_slots::countEnabled(pageMask, pageCount);
    if (deleteCount == 0U || deleteCount >= pageCount) return false;

    bool changed = false;
    for (int page = static_cast<int>(pageCount) - 1; page >= 0; --page) {
        const auto pageIndex = static_cast<uint8_t>(page);
        if ((pageMask & structure_slots::slotBit(pageIndex)) == 0U) continue;
        changed =
            core::state::sequencer::removePage(sequencer, pageIndex) ||
            changed;
    }
    return changed;
}

}  // namespace core::handler
