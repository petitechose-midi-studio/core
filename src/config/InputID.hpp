/*
 * InputID.hpp
 *
 * Hardware input identifiers for all buttons and encoders.
 *
 * This file defines unique IDs for every physical control on the device.
 * ButtonID and EncoderID completely replace the old InputID enum.
 * Each consumer must use the appropriate type.
 *
 * ID Structure (decimal ranges):
 * BUTTONS (0-99):
 * - 10-19     Navigation buttons (left side)
 * - 20-29     Navigation buttons (bottom)
 * - 30-39     Encoder integrated buttons (main matrix)
 * - 40-49     Special encoder buttons
 *
 * ENCODERS (300-999):
 * - 301-308   Main encoder matrix (2x4 encoders)
 * - 400-499   Special encoders
 *
 * Encoder/Button correspondence (mathematical pattern):
 * - ButtonID::MACRO_1 (31) <-> EncoderID::MACRO_1 (301)
 * - ButtonID::NAV (40) <-> EncoderID::NAV (400)
 * - ButtonID::OPT (41) <-> EncoderID::OPT (410)
 *
 * To add a new control:
 * 1. Add an enum value below (choose an appropriate ID number based on type)
 * 2. Add the hardware definition in InputDefinition.hpp
 * 3. Optionally add a MIDI mapping in MidiMapping.hpp
 *
 * Dependencies: None
 * This file has zero dependencies and can be included anywhere.
 */

#pragma once
#include <cstdint>

/*
 * ButtonID
 *
 * Strongly-typed identifiers for all hardware buttons (including encoder buttons).
 * Range: 0-99
 */
enum class ButtonID : uint16_t {
    /* Left side navigation buttons (10-19) */
    LEFT_TOP = 10,
    LEFT_CENTER = 11,
    LEFT_BOTTOM = 12,

    /* Bottom navigation buttons (20-29) */
    BOTTOM_LEFT = 20,
    BOTTOM_CENTER = 21,
    BOTTOM_RIGHT = 22,

    /* Main encoder integrated buttons (30-39) */
    MACRO_1 = 31,
    MACRO_2 = 32,
    MACRO_3 = 33,
    MACRO_4 = 34,
    MACRO_5 = 35,
    MACRO_6 = 36,
    MACRO_7 = 37,
    MACRO_8 = 38,

    /* Special encoder buttons (40-49) */
    NAV = 40,
};

/*
 * EncoderID
 *
 * Strongly-typed identifiers for all hardware encoders.
 * Range: 300-999
 */
enum class EncoderID : uint16_t {
    /* Main encoder matrix (301-308) */
    MACRO_1 = 301,
    MACRO_2 = 302,
    MACRO_3 = 303,
    MACRO_4 = 304,
    MACRO_5 = 305,
    MACRO_6 = 306,
    MACRO_7 = 307,
    MACRO_8 = 308,

    /* Special encoders (400-499) */
    NAV = 400,
    OPT = 410,
};
