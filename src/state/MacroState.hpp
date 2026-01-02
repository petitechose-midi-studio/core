#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

namespace state {

using oc::state::Signal;
using oc::state::SignalLabel;
using oc::state::SignalTiny;

static constexpr uint8_t MACRO_COUNT = 8;

/**
 * @brief Single macro slot state
 */
struct MacroSlot {
    Signal<float> value{0.5f};     ///< Normalized value [0.0, 1.0]
    SignalLabel label;              ///< Display label ("Macro 1")
    SignalTiny displayValue;        ///< CC value as string ("64")

    /// Update displayValue from current value (CC 0-127)
    void updateDisplayValue() {
        uint8_t cc = static_cast<uint8_t>(value.get() * 127.0f);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", cc);
        displayValue.set(buf);
    }

    /// Reset to center position
    void reset() {
        value.set(0.5f);
        updateDisplayValue();
    }
};

/**
 * @brief Reactive state for 8 macro parameters
 *
 * Each macro has a normalized value [0.0, 1.0], label, and display value.
 * UI components subscribe to these signals for automatic updates.
 */
struct MacroState {
    MacroSlot slots[MACRO_COUNT];

    MacroState() {
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            char buf[16];
            snprintf(buf, sizeof(buf), "Macro %d", i + 1);
            slots[i].label.set(buf);
            slots[i].updateDisplayValue();
        }
    }

    MacroSlot& operator[](uint8_t index) { return slots[index]; }
    const MacroSlot& operator[](uint8_t index) const { return slots[index]; }
};

}  // namespace state
