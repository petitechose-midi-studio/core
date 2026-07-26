#pragma once

#include <cstdint>

namespace core::state::shared {

/**
 * Reusable slot-mask operations for page and track structure navigation.
 *
 * Callers provide the domain-specific copy operation; this header owns mask
 * mutation and enabled-slot navigation.
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

NavigationTarget nextNavigationTarget(
    uint16_t enabledMask,
    uint8_t current,
    uint8_t count,
    bool currentAddSlot,
    int direction
);

}  // namespace core::state::shared
