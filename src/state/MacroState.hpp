#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

namespace state {

using oc::state::Signal;
using oc::state::SignalLabel;

static constexpr uint8_t MACRO_COUNT = 8;

/**
 * @brief Reactive state for 8 macro parameters
 *
 * Each macro has a normalized value [0.0, 1.0] and a label.
 * UI components subscribe to these signals for automatic updates.
 */
struct MacroState {
    Signal<float> values[MACRO_COUNT] = {
        Signal<float>{0.5f}, Signal<float>{0.5f},
        Signal<float>{0.5f}, Signal<float>{0.5f},
        Signal<float>{0.5f}, Signal<float>{0.5f},
        Signal<float>{0.5f}, Signal<float>{0.5f}
    };

    SignalLabel labels[MACRO_COUNT];

    MacroState() {
        // Initialize default labels
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            char buf[16];
            snprintf(buf, sizeof(buf), "Macro %d", i + 1);
            labels[i].set(buf);
        }
    }
};

}  // namespace state
