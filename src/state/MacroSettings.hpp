#pragma once

/**
 * @file MacroSettings.hpp
 * @brief Persistent storage for macro values
 *
 * Stores macro values to flash memory for recall after power cycle.
 */

#include <cstdint>

#include <oc/state/Settings.hpp>

#include "MacroState.hpp"

namespace state {

/**
 * @brief Persistent data for macro settings
 *
 * Must be trivially copyable (POD) for Settings<T>.
 */
struct MacroSettingsData {
    static constexpr uint16_t VERSION = 1;

    /// Stored values for each macro (normalized 0.0-1.0)
    float values[MACRO_COUNT] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
};

/// Storage address for macro settings (start of settings region)
static constexpr uint32_t MACRO_SETTINGS_ADDRESS = 0x0000;

/**
 * @brief Type alias for macro settings container
 */
using MacroSettings = oc::state::Settings<MacroSettingsData>;

/**
 * @brief Apply saved settings to state
 */
inline void applySettingsToState(const MacroSettingsData& data, MacroState& state) {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        state.slots[i].value.set(data.values[i]);
        state.slots[i].updateDisplayValue();
    }
}

/**
 * @brief Copy state to settings data for saving
 */
inline void copyStateToSettings(const MacroState& state, MacroSettingsData& data) {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        data.values[i] = state.slots[i].value.get();
    }
}

}  // namespace state
