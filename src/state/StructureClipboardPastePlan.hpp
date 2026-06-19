#pragma once

#include <array>
#include <cstdint>

#include "state/StructureClipboardState.hpp"

namespace core::state {

struct SequencerPageSelectionPastePlanEntry {
    uint8_t clipboardIndex = 0;
    uint8_t destinationPage = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
};

struct SequencerPageSelectionPastePlan {
    static constexpr uint8_t MAX_ENTRIES =
        core::state::sequencer::SequencerPatternState::PAGE_COUNT;

    uint16_t destinationMask = 0;
    uint16_t overwriteMask = 0;
    uint8_t firstDestinationPage =
        core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    uint8_t count = 0;
    std::array<SequencerPageSelectionPastePlanEntry, MAX_ENTRIES> entries{};

    bool hasEntries() const { return count > 0; }
};

SequencerPageSelectionPastePlan buildSequencerPageSelectionPastePlan(
    const SequencerPageSelectionClipboard& clipboard,
    uint8_t cursorPage,
    uint8_t activePageCount
);

}  // namespace core::state
