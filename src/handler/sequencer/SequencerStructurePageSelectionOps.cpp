#include "handler/sequencer/SequencerStructurePageSelectionOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

FLASHMEM uint8_t firstSelectedPage(uint16_t mask) {
    for (uint8_t page = 0;
         page < core::state::sequencer::SequencerState::PAGE_COUNT;
         ++page) {
        if ((mask & structure_slots::slotBit(page)) != 0) return page;
    }
    return core::state::sequencer::SequencerState::PAGE_COUNT;
}

FLASHMEM bool clearPage(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t page
) {
    const uint8_t start = static_cast<uint8_t>(
        page * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    const uint8_t end = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(
            start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1
        )
    ));
    return core::state::sequencer::clearStepRange(sequencer, start, end);
}

}  // namespace

FLASHMEM bool capturePageSelectionClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask,
    core::state::SequencerPageSelectionClipboard& clipboard
) {
    const uint8_t firstPage = firstSelectedPage(selectedMask);
    if (firstPage >= core::state::sequencer::SequencerState::PAGE_COUNT) return false;

    clipboard = {};
    clipboard.valid = true;
    clipboard.sourceFirstPage = firstPage;

    for (uint8_t page = firstPage;
         page < core::state::sequencer::SequencerState::PAGE_COUNT;
         ++page) {
        if ((selectedMask & structure_slots::slotBit(page)) == 0) continue;
        if (clipboard.count >= clipboard.pages.size()) break;

        auto& entry = clipboard.pages[clipboard.count];
        if (!capturePageClipboard(sequencer, page, entry)) continue;
        ++clipboard.count;
    }

    return clipboard.count > 0;
}

FLASHMEM uint16_t activePageSelectionMask(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
) {
    return static_cast<uint16_t>(
        selectedMask & structure_slots::prefixMask(sequencer.activePageCount())
    );
}

FLASHMEM core::state::SequencerPageSelectionPastePlan buildPageSelectionPastePlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    uint8_t cursorPage
) {
    if (!structureClipboard.hasSequencerPageSelection()) {
        return {};
    }
    return core::state::buildSequencerPageSelectionPastePlan(
        structureClipboard.sequencerPageSelection,
        cursorPage,
        sequencer.activePageCount()
    );
}

FLASHMEM void pastePageSelectionClipboard(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    const core::state::SequencerPageSelectionPastePlan& plan
) {
    const auto& clipboard = structureClipboard.sequencerPageSelection;
    for (uint8_t i = 0; i < plan.count; ++i) {
        const auto& target = plan.entries[i];
        const auto& entry = clipboard.pages[target.clipboardIndex];
        pastePageClipboard(
            sequencer,
            entry,
            structureClipboard.sequencerGraph.get(),
            target.destinationPage
        );
    }
}

FLASHMEM bool clearSelectedPages(
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
) {
    const uint16_t mask = activePageSelectionMask(sequencer, selectedMask);
    if (mask == 0) return false;

    bool changed = false;
    const uint8_t pageCount = sequencer.activePageCount();
    for (uint8_t page = 0; page < pageCount; ++page) {
        if ((mask & structure_slots::slotBit(page)) == 0) continue;
        changed = clearPage(sequencer, page) || changed;
    }
    if (changed) {
        sequencer.pattern.bumpStepDataRevision();
    }
    return changed;
}

FLASHMEM bool removeSelectedPages(
    core::state::sequencer::SequencerState& sequencer,
    uint16_t selectedMask
) {
    const uint8_t pageCount = sequencer.activePageCount();
    const uint16_t mask = activePageSelectionMask(sequencer, selectedMask);
    const uint8_t deleteCount = structure_slots::countEnabled(mask, pageCount);
    if (deleteCount == 0 || deleteCount >= pageCount) return false;

    bool changed = false;
    for (int page = static_cast<int>(pageCount) - 1; page >= 0; --page) {
        const uint8_t pageIndex = static_cast<uint8_t>(page);
        if ((mask & structure_slots::slotBit(pageIndex)) == 0) continue;
        changed = core::state::sequencer::removePage(sequencer, pageIndex) || changed;
    }
    return changed;
}

}  // namespace core::handler
