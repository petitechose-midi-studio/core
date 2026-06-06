#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

inline constexpr unsigned int kStructureNavigationFocusMaxSubscribers = 8;

/**
 * Shared structure navigation primitives for page/track selection flows.
 *
 * These structs intentionally hold only focus, selection masks, and hold action
 * timing. Domain-specific add/remove/copy behavior lives in workflow/ops code.
 */
enum class StructureNavigationFocus : uint8_t {
    PAGE = 0,
    TRACK = 1,
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

inline constexpr StructureSelectionScope selectionScopeForFocus(StructureNavigationFocus focus) {
    switch (focus) {
        case StructureNavigationFocus::TRACK:
            return StructureSelectionScope::TRACK;
        case StructureNavigationFocus::PAGE:
        default:
            return StructureSelectionScope::PAGE;
    }
}

struct StructureSelectionState {
    oc::state::Signal<bool, 8> active{false};
    oc::state::Signal<StructureSelectionScope, 8> scope{StructureSelectionScope::PAGE};
    oc::state::Signal<uint8_t, 8> cursorIndex{0};
    oc::state::Signal<uint16_t, 16> selectedMask{0};

    StructureSelectionState();

    void reset(StructureSelectionScope focus = StructureSelectionScope::PAGE, uint8_t cursor = 0);
};

struct StructureHoldState {
    oc::state::Signal<StructureHoldAction, 4> action{StructureHoldAction::NONE};
    oc::state::Signal<uint32_t, 4> startedAtMs{0};

    bool active() const;

    void begin(StructureHoldAction nextAction, uint32_t nowMs);

    void clear();
};

}  // namespace core::state
