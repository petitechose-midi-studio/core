#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

inline constexpr unsigned int kStructureNavigationFocusMaxSubscribers = 8;

enum class StructureNavigationFocus : uint8_t {
    PAGE = 0,
    TRACK = 1,
    STEP = 2,
    COUNT = 3,
};

enum class StructureHoldAction : uint8_t {
    NONE = 0,
    REMOVE = 1,
    PASTE = 2,
    COUNT = 3,
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
    // Exactly two product consumers observe hold progress: Sequencer and
    // Macro presentation. Acquisition identity is private and non-reactive.
    oc::state::Signal<uint32_t, 2> startedAtMs{0};

    bool active() const;
    [[nodiscard]] uint32_t acquisitionId() const {
        return acquisition_id_;
    }
    void begin(StructureHoldAction nextAction, uint32_t nowMs);
    void clear();

private:
    uint32_t acquisition_id_ = 0U;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#if OC_ENABLE_STATS
static_assert(
    sizeof(StructureHoldState) == 116U,
    "diagnostic Structure hold state must retain its bounded ARM envelope"
);
#else
static_assert(
    sizeof(StructureHoldState) == 108U,
    "Structure hold state must retain its compact ARM signal envelope"
);
#endif
#endif

}  // namespace core::state
