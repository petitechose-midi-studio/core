#pragma once

/**
 * @file MacroInputHandler.hpp
 * @brief Handles encoder input for macro controls
 *
 * Binds encoders to macro state and sends MIDI CC output.
 * Uses page configuration for CC/channel mapping.
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"

namespace handler {

/**
 * @brief Encoder input handler for standalone macros
 *
 * Handles encoder turns → updates state → sends MIDI CC.
 * Uses page configuration for CC/channel mapping.
 * Bindings are scoped to the provided LVGL element.
 */
class MacroInputHandler {
public:
    MacroInputHandler(state::CoreState& coreState,
                      oc::api::EncoderAPI& encoders,
                      oc::api::MidiAPI& midi,
                      lv_obj_t* scopeElement);

    ~MacroInputHandler() = default;

    MacroInputHandler(const MacroInputHandler&) = delete;
    MacroInputHandler& operator=(const MacroInputHandler&) = delete;

private:
    static constexpr std::array<Config::EncoderID, state::MACRO_COUNT> ENCODERS = {
        Config::EncoderID::MACRO_1, Config::EncoderID::MACRO_2,
        Config::EncoderID::MACRO_3, Config::EncoderID::MACRO_4,
        Config::EncoderID::MACRO_5, Config::EncoderID::MACRO_6,
        Config::EncoderID::MACRO_7, Config::EncoderID::MACRO_8
    };

    void setupBindings();
    void handleValueChange(uint8_t index, float value);

    state::CoreState& coreState_;
    oc::api::EncoderAPI& encoders_;
    oc::api::MidiAPI& midi_;
    lv_obj_t* scopeElement_;
};

}  // namespace handler
