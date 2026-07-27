#pragma once

#include <array>
#include <cstdint>

#include "state/StructureClipboardPastePlan.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::state::macro {

struct MacroSlotClipboardPlanEntry {
    uint8_t clipboardIndex = 0U;
    uint8_t sourceLinear = 0U;
    uint8_t targetLinear = 0U;
    uint8_t targetPage = PAGE_COUNT;
    uint8_t targetMacro = MACRO_COUNT;
    bool overwrite = false;
};

/**
 * Non-mutating sparse Macro Slot placement.
 *
 * Source offsets are preserved relative to the first copied Slot. Placement
 * may target every existing Page plus exactly one synthetic next Page. No
 * entry is clipped or shifted to avoid a collision.
 */
struct MacroSlotClipboardPlan {
    static constexpr uint8_t SLOT_COUNT = PAGE_COUNT * MACRO_COUNT;
    static constexpr uint8_t MAX_ENTRIES = SLOT_COUNT;

    uint32_t clipboardRevision = 0U;
    uint8_t sourceCount = 0U;
    uint8_t count = 0U;
    uint8_t firstSourceLinear = SLOT_COUNT;
    uint8_t lastSourceLinear = SLOT_COUNT;
    uint8_t targetTrack = TRACK_COUNT;
    uint8_t anchorLinear = SLOT_COUNT;
    uint8_t firstTargetLinear = SLOT_COUNT;
    uint8_t lastTargetLinear = SLOT_COUNT;
    uint8_t existingPageCount = 0U;
    uint8_t allowedPageCount = 0U;
    uint8_t requiredPageCount = 0U;
    uint8_t overwriteCount = 0U;
    uint16_t createPageMask = 0U;
    ClipboardTransferAvailability availability =
        ClipboardTransferAvailability::DISABLED;
    ClipboardTransferReason reason =
        ClipboardTransferReason::EMPTY_CLIPBOARD;
    std::array<uint8_t, PAGE_COUNT> destinationMasks{};
    std::array<uint8_t, PAGE_COUNT> overwriteMasks{};
    std::array<MacroSlotClipboardPlanEntry, MAX_ENTRIES> entries{};

    [[nodiscard]] bool hasEntries() const { return count > 0U; }
    [[nodiscard]] bool canCommit() const {
        return availability != ClipboardTransferAvailability::DISABLED &&
               sourceCount > 0U && count == sourceCount;
    }
};

MacroSlotClipboardPlan buildMacroSlotClipboardPlan(
    const core::state::StructureClipboardState& clipboard,
    const MacroPagesState& pages,
    uint8_t targetTrack,
    uint8_t anchorLinear
);

bool sameMacroSlotClipboardPlan(
    const MacroSlotClipboardPlan& lhs,
    const MacroSlotClipboardPlan& rhs
);

}  // namespace core::state::macro
