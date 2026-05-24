#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

enum class SequencerSettingsFlowPhase : uint8_t {
    CLOSED = 0,
    OVERLAY = 1,
};

struct SequencerSettingsState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<SequencerSettingsFlowPhase, 4> flowPhase{
        SequencerSettingsFlowPhase::CLOSED
    };

    void reset() {
        visible.set(false);
        focusedRow.set(0);
        flowPhase.set(SequencerSettingsFlowPhase::CLOSED);
    }

    void openOverlay() {
        reset();
        visible.set(true);
        flowPhase.set(SequencerSettingsFlowPhase::OVERLAY);
    }

    void closeOverlay() {
        reset();
    }
};

}  // namespace core::state
