#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

inline constexpr unsigned int kStructureNavigationFocusMaxSubscribers = 8;

enum class StructureNavigationFocus : uint8_t {
    PAGE = 0,
    TRACK = 1,
    STEP = 2,
};

enum class StructureHoldAction : uint8_t {
    NONE = 0,
    REMOVE = 1,
    PASTE = 2,
};

enum class StructureSelectionScope : uint8_t {
    PAGE = 0,
    TRACK = 1,
};

struct StructureSelectionState {
    oc::state::Signal<bool, 8> active{false};
    oc::state::Signal<bool, 8> placing{false};
    oc::state::Signal<StructureSelectionScope, 8> scope{
        StructureSelectionScope::PAGE
    };
    oc::state::Signal<uint8_t, 8> cursorIndex{0};
    oc::state::Signal<uint16_t, 16> selectedMask{0};
    oc::state::Signal<uint16_t, 16> destinationMask{0};
    oc::state::Signal<uint16_t, 16> overwriteMask{0};
    oc::state::Signal<bool, 8> pasteBlocked{false};
    oc::state::Signal<uint32_t, 8> clipboardRevision{0};

    void reset(
        StructureSelectionScope nextScope = StructureSelectionScope::PAGE,
        uint8_t cursor = 0
    );

    [[nodiscard]] bool anySelected() const {
        return selectedMask.get() != 0U;
    }

    [[nodiscard]] bool placementActive() const {
        return active.get() && placing.get();
    }

    /**
     * Clears the current sparse selection/placement while keeping the
     * selection scope armed at the current cursor.
     */
    void clearCurrent();
};

struct StructureHoldState {
    oc::state::Signal<StructureHoldAction, 4> action{StructureHoldAction::NONE};
    oc::state::Signal<uint32_t, 4> startedAtMs{0};

    bool active() const;
    void begin(StructureHoldAction nextAction, uint32_t nowMs);
    void clear();
};

}  // namespace core::state
