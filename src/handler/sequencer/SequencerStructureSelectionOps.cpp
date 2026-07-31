#include "handler/sequencer/SequencerStructureSelectionOps.hpp"

#include <algorithm>
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

FLASHMEM bool deleteSelectedRootPages(
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
            core::state::sequencer::deletePage(sequencer, pageIndex) ||
            changed;
    }
    return changed;
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

FLASHMEM bool pastePageSelectionClipboard(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    const core::state::SequencerPageSelectionPastePlan& plan
) {
    using Pattern =
        core::state::sequencer::SequencerPatternState;
    if (!plan.canCommit() ||
        !structureClipboard.hasSequencerPageSelection()) {
        return false;
    }

    const uint8_t oldLength = sequencer.pattern.length.get();
    const auto& clipboard = structureClipboard.sequencerPageSelection;
    const auto& lastTarget = plan.entries[plan.count - 1U];
    if (lastTarget.clipboardIndex >= clipboard.count) return false;
    const auto& lastSource =
        clipboard.pages[lastTarget.clipboardIndex];
    const uint16_t requiredLengthWide =
        static_cast<uint16_t>(lastTarget.destinationPage) *
            Pattern::STEPS_PER_PAGE +
        std::max<uint8_t>(lastSource.count, 1U);
    const uint8_t requiredLength = static_cast<uint8_t>(
        std::min<uint16_t>(requiredLengthWide, Pattern::MAX_STEPS)
    );

    if (requiredLength > oldLength) {
        sequencer.pattern.setContentLength(requiredLength);
        (void)core::state::sequencer::clearStepRange(
            sequencer,
            oldLength,
            static_cast<uint8_t>(requiredLength - 1U)
        );
    }

    for (uint8_t index = 0; index < plan.count; ++index) {
        const auto& target = plan.entries[index];
        if (target.clipboardIndex >= clipboard.count ||
            target.destinationPage >= Pattern::PAGE_COUNT) {
            return false;
        }
        pastePageClipboard(
            sequencer,
            clipboard.pages[target.clipboardIndex],
            structureClipboard.sequencerGraph.get(),
            target.destinationPage
        );
    }
    return true;
}

}  // namespace core::handler
