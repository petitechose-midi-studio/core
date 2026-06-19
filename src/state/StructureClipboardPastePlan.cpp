#include "state/StructureClipboardPastePlan.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"

namespace core::state {

FLASHMEM SequencerPageSelectionPastePlan buildSequencerPageSelectionPastePlan(
    const SequencerPageSelectionClipboard& clipboard,
    uint8_t cursorPage,
    uint8_t activePageCount
) {
    SequencerPageSelectionPastePlan plan;
    if (!clipboard.valid || clipboard.count == 0) return plan;
    if (clipboard.sourceFirstPage >= core::state::sequencer::SequencerPatternState::PAGE_COUNT) {
        return plan;
    }

    const uint8_t pageLimit = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    const uint8_t clampedCursor = std::min<uint8_t>(
        cursorPage,
        static_cast<uint8_t>(pageLimit - 1U)
    );
    const uint8_t clampedActivePageCount = std::min<uint8_t>(activePageCount, pageLimit);

    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const auto& page = clipboard.pages[i];
        if (!page.valid || page.sourcePage < clipboard.sourceFirstPage) continue;

        const uint16_t destination = static_cast<uint16_t>(clampedCursor) +
            static_cast<uint16_t>(page.sourcePage - clipboard.sourceFirstPage);
        if (destination >= pageLimit) continue;
        if (plan.count >= plan.entries.size()) break;

        const auto destinationPage = static_cast<uint8_t>(destination);
        plan.entries[plan.count++] = {
            .clipboardIndex = i,
            .destinationPage = destinationPage,
        };
        plan.destinationMask = static_cast<uint16_t>(
            plan.destinationMask | core::state::shared::slotBit(destinationPage)
        );
        if (destinationPage < clampedActivePageCount) {
            plan.overwriteMask = static_cast<uint16_t>(
                plan.overwriteMask | core::state::shared::slotBit(destinationPage)
            );
        }
        plan.firstDestinationPage = std::min(plan.firstDestinationPage, destinationPage);
    }

    return plan;
}

}  // namespace core::state
