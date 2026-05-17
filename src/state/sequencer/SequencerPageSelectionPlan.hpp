#pragma once

#include <array>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

struct SequencerPageDuplicateEntry {
    uint8_t sourcePage = 0;
    uint8_t destinationPage = 0;
};

struct SequencerPageDuplicatePlan {
    uint16_t destinationMask = 0;
    uint16_t overwriteMask = 0;
    uint8_t entryCount = 0;
    std::array<SequencerPageDuplicateEntry, SequencerState::PAGE_COUNT> entries{};

    bool hasEntries() const {
        return entryCount > 0;
    }

    bool movesAnyPage() const {
        for (uint8_t i = 0; i < entryCount; ++i) {
            if (entries[i].sourcePage != entries[i].destinationPage) {
                return true;
            }
        }
        return false;
    }
};

SequencerPageDuplicatePlan buildPageDuplicatePlan(
    const SequencerState& sequencer,
    uint16_t selectedMask,
    uint8_t cursorPage
);

}  // namespace core::state::sequencer
