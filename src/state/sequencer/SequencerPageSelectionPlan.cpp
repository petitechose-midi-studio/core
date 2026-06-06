#include "state/sequencer/SequencerPageSelectionPlan.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

FLASHMEM uint16_t pageBit(uint8_t page) {
    return static_cast<uint16_t>(1U << page);
}

FLASHMEM uint16_t activePageMask(uint8_t pageCount) {
    if (pageCount >= SequencerState::PAGE_COUNT) {
        return static_cast<uint16_t>((1U << SequencerState::PAGE_COUNT) - 1U);
    }
    return static_cast<uint16_t>((1U << pageCount) - 1U);
}

FLASHMEM uint8_t findFirstSelectedPage(uint16_t selectedMask) {
    for (uint8_t page = 0; page < SequencerState::PAGE_COUNT; ++page) {
        if ((selectedMask & pageBit(page)) != 0) {
            return page;
        }
    }
    return SequencerState::PAGE_COUNT;
}

}  // namespace

FLASHMEM SequencerPageDuplicatePlan buildPageDuplicatePlan(
    const SequencerState& sequencer,
    uint16_t selectedMask,
    uint8_t cursorPage
) {
    SequencerPageDuplicatePlan plan{};
    const uint8_t clampedCursorPage = sequencer.clampPage(cursorPage);

    const uint8_t activePages = sequencer.activePageCount();
    const uint16_t normalizedSourceMask = static_cast<uint16_t>(
        selectedMask & activePageMask(activePages)
    );
    const uint8_t firstSourcePage = findFirstSelectedPage(normalizedSourceMask);
    if (firstSourcePage >= SequencerState::PAGE_COUNT) {
        return plan;
    }

    for (uint8_t sourcePage = 0; sourcePage < SequencerState::PAGE_COUNT; ++sourcePage) {
        const uint16_t sourceBit = pageBit(sourcePage);
        if ((normalizedSourceMask & sourceBit) == 0) continue;

        const uint8_t offset = static_cast<uint8_t>(sourcePage - firstSourcePage);
        const uint16_t destination = static_cast<uint16_t>(clampedCursorPage) + offset;
        if (destination >= SequencerState::PAGE_COUNT) {
            continue;
        }

        const uint8_t destinationPage = static_cast<uint8_t>(destination);
        const uint16_t destinationBit = pageBit(destinationPage);
        const bool overwrites = destinationPage < activePages;

        plan.destinationMask = static_cast<uint16_t>(plan.destinationMask | destinationBit);
        if (overwrites) {
            plan.overwriteMask = static_cast<uint16_t>(plan.overwriteMask | destinationBit);
        }

        if (plan.entryCount < plan.entries.size()) {
            plan.entries[plan.entryCount++] = SequencerPageDuplicateEntry{
                .sourcePage = sourcePage,
                .destinationPage = destinationPage,
            };
        }
    }

    return plan;
}

}  // namespace core::state::sequencer
