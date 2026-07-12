#pragma once

#include <cstdint>

namespace core::state::shared {

/**
 * Reusable slot-mask operations for page and track structure navigation.
 *
 * Callers provide the domain-specific copy operation; this header owns mask
 * mutation, enabled-slot navigation, and selection duplication rules.
 */
struct NavigationTarget {
    uint8_t index = 0;
    bool addSlot = false;
    bool valid = false;
};

struct MaskMutation {
    uint16_t nextMask = 0;
    uint8_t nextActive = 0;
    bool changed = false;
};

struct DuplicationResult {
    uint16_t nextMask = 0;
    uint8_t firstDuplicated = 0;
    bool changed = false;
};

uint16_t slotBit(uint8_t index);

uint16_t prefixMask(uint8_t count);

bool isEnabled(uint16_t enabledMask, uint8_t index);

uint8_t countEnabled(uint16_t enabledMask, uint8_t count);

uint8_t firstEnabledIndex(uint16_t enabledMask, uint8_t count);

uint8_t lastEnabledIndex(uint16_t enabledMask, uint8_t count);

uint8_t wrapIndex(uint8_t current, int direction, uint8_t count);

uint8_t nextEnabledIndex(uint16_t enabledMask, uint8_t current, uint8_t count, int direction = 1);

int nextAddIndexAfterHighest(uint16_t enabledMask, uint8_t count);

int firstDisabledIndex(uint16_t enabledMask, uint8_t count);

MaskMutation removeIndex(uint16_t enabledMask, uint8_t current, uint8_t count);

MaskMutation removeSelected(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t current,
    uint8_t count
);

template <typename CopyFn>
inline DuplicationResult duplicateSelectionIntoFreeSlots(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t count,
    CopyFn&& copyFn
) {
    uint16_t nextMask = enabledMask;
    uint8_t firstDuplicated = count;

    for (uint8_t source = 0; source < count; ++source) {
        const uint16_t sourceBit = slotBit(source);
        if ((selectedMask & sourceBit) == 0 || (enabledMask & sourceBit) == 0) {
            continue;
        }

        const int dest = firstDisabledIndex(nextMask, count);
        if (dest < 0) {
            break;
        }

        if (!copyFn(source, static_cast<uint8_t>(dest))) {
            break;
        }
        nextMask |= slotBit(static_cast<uint8_t>(dest));
        if (firstDuplicated >= count) {
            firstDuplicated = static_cast<uint8_t>(dest);
        }
    }

    return {
        .nextMask = nextMask,
        .firstDuplicated = firstDuplicated,
        .changed = nextMask != enabledMask,
    };
}

NavigationTarget nextNavigationTarget(
    uint16_t enabledMask,
    uint8_t current,
    uint8_t count,
    bool currentAddSlot,
    int direction
);

}  // namespace core::state::shared
