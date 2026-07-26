#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

enum class SequencerSettingsFlowPhase : uint8_t {
    CLOSED = 0,
    OVERLAY = 1,
    VALUE_SELECTOR = 2,
};

struct SequencerSettingsValueSelectorState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> editingRow{0};
    oc::state::Signal<int> selectedIndex{0};

    void reset();
};

struct SequencerSettingsState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<SequencerSettingsFlowPhase, 4> flowPhase{
        SequencerSettingsFlowPhase::CLOSED
    };
    SequencerSettingsValueSelectorState selector;

    void reset();
    void openOverlay();
    void closeOverlay();
    void openSelector(uint8_t row, int selected);
    void closeSelector();
};

}  // namespace core::state
