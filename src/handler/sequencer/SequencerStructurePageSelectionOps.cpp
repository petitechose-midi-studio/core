#include "handler/sequencer/SequencerStructurePageSelectionOps.hpp"

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"
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

}  // namespace core::handler
