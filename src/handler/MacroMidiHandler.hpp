#pragma once

/**
 * @file MacroMidiHandler.hpp
 * @brief Handles incoming MIDI CC for macro controls
 *
 * Receives MIDI CC and updates macro state + encoder positions.
 * Uses page configuration for CC/channel mapping.
 */

#include <cstdint>

#include <oc/api/EncoderAPI.hpp>
#include <oc/api/MidiAPI.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"

namespace handler {

/**
 * @brief MIDI input handler for standalone macros
 *
 * Handles incoming MIDI CC → updates state → syncs encoder position.
 * Uses page configuration for CC/channel matching.
 */
class MacroMidiHandler {
public:
    MacroMidiHandler(state::CoreState& coreState,
                     oc::api::MidiAPI& midi,
                     oc::api::EncoderAPI& encoders);

    ~MacroMidiHandler() = default;

    MacroMidiHandler(const MacroMidiHandler&) = delete;
    MacroMidiHandler& operator=(const MacroMidiHandler&) = delete;

private:
    static constexpr std::array<Config::EncoderID, state::MACRO_COUNT> ENCODERS = {
        Config::EncoderID::MACRO_1, Config::EncoderID::MACRO_2,
        Config::EncoderID::MACRO_3, Config::EncoderID::MACRO_4,
        Config::EncoderID::MACRO_5, Config::EncoderID::MACRO_6,
        Config::EncoderID::MACRO_7, Config::EncoderID::MACRO_8
    };

    void setupCallbacks();
    void handleIncomingCC(uint8_t channel, uint8_t cc, uint8_t value);

    /// Find macro index for given CC/channel (-1 if not found)
    int8_t findMacroForCC(uint8_t channel, uint8_t cc) const;

    state::CoreState& coreState_;
    oc::api::MidiAPI& midi_;
    oc::api::EncoderAPI& encoders_;
};

}  // namespace handler
