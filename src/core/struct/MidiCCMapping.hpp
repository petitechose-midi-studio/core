#pragma once

#include <cstdint>

#include "config/InputID.hpp"

/**
 * @brief Minimal MIDI CC mapping definition
 *
 * Maps a hardware control (button or encoder) to a MIDI Control Change message.
 * Pure data structure with no logic - just the wiring between hardware and MIDI protocol.
 *
 * Type-safe constructors ensure only ButtonID or EncoderID can be used for mappings,
 * providing IDE autocomplete and preventing invalid input IDs at compile time.
 */
struct MidiCCMapping {
    uint16_t inputId;
    uint8_t channel;
    uint8_t cc;

    constexpr MidiCCMapping(ButtonID buttonId, uint8_t channel, uint8_t cc)
        : inputId(static_cast<uint16_t>(buttonId)), channel(channel), cc(cc) {}

    constexpr MidiCCMapping(EncoderID encoderId, uint8_t channel, uint8_t cc)
        : inputId(static_cast<uint16_t>(encoderId)), channel(channel), cc(cc) {}
};
