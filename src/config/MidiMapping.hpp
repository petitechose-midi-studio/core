/*
 * MidiMapping.hpp
 *
 * Default MIDI CC mappings for all hardware controls.
 *
 * This file defines how each physical control (button or encoder) maps to
 * MIDI Control Change (CC) messages. Each entry specifies:
 * - Which control (InputID) is mapped
 * - Which MIDI channel to send on (0-15)
 * - Which CC number to use (0-127)
 *
 * Format:
 *   {InputID, midiChannel, ccNumber}
 *
 * MIDI CC ranges used:
 * - CC 1-10    Main encoders
 * - CC 11-18   Encoder buttons
 * - CC 19-25   Navigation buttons
 *
 * Note: These are the default mappings used when no plugin is active.
 * Active plugins (e.g., Bitwig integration) may override these mappings
 * with their own protocol (SysEx, etc.).
 *
 * To modify:
 * - Change the channel number (0-15) to send on a different MIDI channel
 * - Change the CC number (0-127) to use a different CC parameter
 * - Add/remove entries to map more or fewer controls
 */

#pragma once

#include <cstddef>

#include "InputID.hpp"
#include "core/struct/MidiCCMapping.hpp"

namespace Config {

/*
 * Default MIDI CC mappings
 *
 * Maps each hardware control to a MIDI CC message.
 * All mappings use channel 0 by default.
 */
constexpr MidiCCMapping MIDI_MAPPINGS[] = {
    /* Main encoders (CC 1-10) */
    {EncoderID::MACRO_1, 0, 1},
    {EncoderID::MACRO_2, 0, 2},
    {EncoderID::MACRO_3, 0, 3},
    {EncoderID::MACRO_4, 0, 4},
    {EncoderID::MACRO_5, 0, 5},
    {EncoderID::MACRO_6, 0, 6},
    {EncoderID::MACRO_7, 0, 7},
    {EncoderID::MACRO_8, 0, 8},
    {EncoderID::NAV, 0, 9},
    {EncoderID::OPT, 0, 10},

    /* Encoder buttons (CC 11-18) */
    {ButtonID::MACRO_1, 0, 11},
    {ButtonID::MACRO_2, 0, 12},
    {ButtonID::MACRO_3, 0, 13},
    {ButtonID::MACRO_4, 0, 14},
    {ButtonID::MACRO_5, 0, 15},
    {ButtonID::MACRO_6, 0, 16},
    {ButtonID::MACRO_7, 0, 17},
    {ButtonID::MACRO_8, 0, 18},

    /* Navigation buttons (CC 19-25) */
    {ButtonID::LEFT_TOP, 0, 19},
    {ButtonID::LEFT_CENTER, 0, 20},
    {ButtonID::LEFT_BOTTOM, 0, 21},
    {ButtonID::BOTTOM_LEFT, 0, 22},
    {ButtonID::BOTTOM_CENTER, 0, 23},
    {ButtonID::BOTTOM_RIGHT, 0, 24},
    {ButtonID::NAV, 0, 25},
};

/*
 * Computed array size (compile-time constant)
 */
constexpr size_t MIDI_MAPPING_COUNT = sizeof(MIDI_MAPPINGS) / sizeof(MIDI_MAPPINGS[0]);

}  // namespace Config
