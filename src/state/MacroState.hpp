#pragma once

/**
 * @file MacroState.hpp
 * @brief Macro parameter state management
 */

#include <cstdint>
#include <cstdio>

#include <oc/state/DerivedSignal.hpp>
#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

#include "InputIDs.hpp"

namespace core::state {

using oc::state::DerivedStringSignal;
using oc::state::Signal;
using oc::state::SignalLabel;

/// Re-export from Config for convenience within state namespace
static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;

/**
 * @brief Single macro slot state
 *
 * displayValue is automatically derived from value - no manual sync needed.
 */
struct MacroSlot {
    Signal<float> value{0.5f};                   ///< Normalized value [0.0, 1.0]
    SignalLabel label;                            ///< Display label ("Macro 1")
    DerivedStringSignal<float, 8> displayValue;   ///< CC value as string ("64"), auto-updated

    /// Construct with auto-derived displayValue
    MacroSlot()
        : displayValue(value, [](float v, char* buf, size_t size) {
              uint8_t cc = static_cast<uint8_t>(v * 127.0f);
              std::snprintf(buf, size, "%d", cc);
          }) {}

    // Non-copyable, non-movable (signals hold references)
    MacroSlot(const MacroSlot&) = delete;
    MacroSlot& operator=(const MacroSlot&) = delete;
    MacroSlot(MacroSlot&&) = delete;
    MacroSlot& operator=(MacroSlot&&) = delete;

    /// Reset to center position (displayValue updates automatically)
    void reset() { value.set(0.5f); }
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
            std::snprintf(buf, sizeof(buf), "Macro %d", i + 1);
            slots[i].label.set(buf);
            // displayValue is auto-derived from value, no manual sync needed
        }
    }

    MacroSlot& operator[](uint8_t index) { return slots[index]; }
    const MacroSlot& operator[](uint8_t index) const { return slots[index]; }
};

}  // namespace core::state
