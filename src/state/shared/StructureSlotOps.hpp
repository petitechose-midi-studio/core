#pragma once

#include <cstdint>

namespace core::state::shared {

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

inline uint16_t slotBit(uint8_t index) {
    return static_cast<uint16_t>(1U << index);
}

inline bool isEnabled(uint16_t enabledMask, uint8_t index) {
    return (enabledMask & slotBit(index)) != 0;
}

inline uint8_t countEnabled(uint16_t enabledMask, uint8_t count) {
    uint8_t enabled = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (isEnabled(enabledMask, i)) {
            ++enabled;
        }
    }
    return enabled;
}

inline uint8_t firstEnabledIndex(uint16_t enabledMask, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        if (isEnabled(enabledMask, i)) {
            return i;
        }
    }
    return 0;
}

inline uint8_t lastEnabledIndex(uint16_t enabledMask, uint8_t count) {
    for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
        const auto index = static_cast<uint8_t>(i);
        if (isEnabled(enabledMask, index)) {
            return index;
        }
    }
    return 0;
}

inline uint8_t wrapIndex(uint8_t current, int direction, uint8_t count) {
    if (count == 0) return 0;
    const int wrapped =
        (static_cast<int>(current) + direction + static_cast<int>(count)) %
        static_cast<int>(count);
    return static_cast<uint8_t>(wrapped);
}

inline uint8_t nextEnabledIndex(uint16_t enabledMask, uint8_t current, uint8_t count, int direction = 1) {
    if (count == 0) return current;

    for (uint8_t offset = 1; offset < count; ++offset) {
        const int candidate =
            (static_cast<int>(current) +
             (direction * static_cast<int>(offset)) +
             static_cast<int>(count)) %
            static_cast<int>(count);
        const auto index = static_cast<uint8_t>(candidate);
        if (isEnabled(enabledMask, index)) {
            return index;
        }
    }

    return current;
}

inline int nextAddIndexAfterHighest(uint16_t enabledMask, uint8_t count) {
    for (int index = static_cast<int>(count) - 1; index >= 0; --index) {
        if (!isEnabled(enabledMask, static_cast<uint8_t>(index))) {
            continue;
        }
        const int next = index + 1;
        return (next < count) ? next : -1;
    }
    return (count > 0) ? 0 : -1;
}

inline int firstDisabledIndex(uint16_t enabledMask, uint8_t count) {
    for (uint8_t index = 0; index < count; ++index) {
        if (!isEnabled(enabledMask, index)) {
            return index;
        }
    }
    return -1;
}

inline MaskMutation removeIndex(uint16_t enabledMask, uint8_t current, uint8_t count) {
    if (!isEnabled(enabledMask, current)) {
        return {.nextMask = enabledMask, .nextActive = current, .changed = false};
    }
    if (countEnabled(enabledMask, count) <= 1U) {
        return {.nextMask = enabledMask, .nextActive = current, .changed = false};
    }

    const uint16_t nextMask = enabledMask & static_cast<uint16_t>(~slotBit(current));
    return {
        .nextMask = nextMask,
        .nextActive = nextEnabledIndex(nextMask, current, count),
        .changed = true,
    };
}

inline MaskMutation removeSelected(
    uint16_t enabledMask,
    uint16_t selectedMask,
    uint8_t current,
    uint8_t count
) {
    const uint16_t deleteMask = enabledMask & selectedMask;
    if (deleteMask == 0) {
        return {.nextMask = enabledMask, .nextActive = current, .changed = false};
    }

    const uint8_t enabledCount = countEnabled(enabledMask, count);
    const uint8_t deleteCount = countEnabled(deleteMask, count);
    if (deleteCount >= enabledCount) {
        return {.nextMask = enabledMask, .nextActive = current, .changed = false};
    }

    const uint16_t nextMask = enabledMask & static_cast<uint16_t>(~deleteMask);
    uint8_t nextActive = current;
    if (!isEnabled(nextMask, nextActive)) {
        nextActive = nextEnabledIndex(nextMask, current, count);
    }
    return {
        .nextMask = nextMask,
        .nextActive = nextActive,
        .changed = true,
    };
}

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

        copyFn(source, static_cast<uint8_t>(dest));
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

inline NavigationTarget nextNavigationTarget(
    uint16_t enabledMask,
    uint8_t current,
    uint8_t count,
    bool currentAddSlot,
    int direction
) {
    const int addIndex = nextAddIndexAfterHighest(enabledMask, count);
    const uint8_t firstEnabled = firstEnabledIndex(enabledMask, count);
    const uint8_t lastEnabled = lastEnabledIndex(enabledMask, count);

    if (currentAddSlot) {
        if (direction < 0) {
            return {.index = lastEnabled, .addSlot = false, .valid = true};
        }
        return {
            .index = (addIndex >= 0) ? static_cast<uint8_t>(addIndex) : lastEnabled,
            .addSlot = (addIndex >= 0),
            .valid = true,
        };
    }

    if (direction > 0) {
        for (uint8_t candidate = static_cast<uint8_t>(current + 1); candidate < count; ++candidate) {
            if (isEnabled(enabledMask, candidate)) {
                return {.index = candidate, .addSlot = false, .valid = true};
            }
        }

        if (addIndex >= 0 && current == lastEnabled) {
            return {
                .index = static_cast<uint8_t>(addIndex),
                .addSlot = true,
                .valid = true,
            };
        }

        return {.index = firstEnabled, .addSlot = false, .valid = true};
    }

    for (int candidate = static_cast<int>(current) - 1; candidate >= 0; --candidate) {
        const auto index = static_cast<uint8_t>(candidate);
        if (isEnabled(enabledMask, index)) {
            return {.index = index, .addSlot = false, .valid = true};
        }
    }

    return {.index = lastEnabled, .addSlot = false, .valid = true};
}

}  // namespace core::state::shared
