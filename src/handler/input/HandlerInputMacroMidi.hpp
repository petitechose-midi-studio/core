#pragma once

/**
 * @file HandlerInputMacroMidi.hpp
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

namespace core::handler {

/**
 * @brief MIDI input handler for standalone macros
 *
 * Handles incoming MIDI CC → updates state → syncs encoder position.
 * Uses page configuration for CC/channel matching.
 */
class HandlerInputMacroMidi {
public:
    HandlerInputMacroMidi(core::state::CoreState& coreState,
                          oc::api::MidiAPI& midi,
                          oc::api::EncoderAPI& encoders);

    ~HandlerInputMacroMidi() = default;

    HandlerInputMacroMidi(const HandlerInputMacroMidi&) = delete;
    HandlerInputMacroMidi& operator=(const HandlerInputMacroMidi&) = delete;

private:
    void setupCallbacks();
    void handleIncomingCC(uint8_t channel, uint8_t cc, uint8_t value);

    /// Find macro index for given CC/channel (-1 if not found)
    int8_t findMacroForCC(uint8_t channel, uint8_t cc) const;

    core::state::CoreState& coreState_;
    oc::api::MidiAPI& midi_;
    oc::api::EncoderAPI& encoders_;
};

}  // namespace core::handler
