/*
 * InputDefinition.hpp
 *
 * Physical hardware definitions for all buttons and encoders.
 *
 * This file contains the complete list of physical input devices
 * connected to the microcontroller. Each entry specifies:
 * - Which logical control (InputID) it represents
 * - Which pins it's connected to (MCU direct or multiplexer)
 * - Hardware-specific parameters (PPR, steps per detent, etc.)
 *
 * Button format:
 *   {InputID, GpioPin}
 *
 * Encoder format:
 *   {InputID, pinA, pinB, pulsesPerRevolution, stepsPerDetent}
 *   Default PPR: 24, Default steps: 1 (can be omitted)
 *
 * Pin helper functions:
 *   mcuPin(n)  - Direct microcontroller pin
 *   muxPin(n)  - Multiplexer channel (0-15)
 *
 * To add a new control:
 * 1. Define its ID in InputID.hpp
 * 2. Add an entry to BUTTONS[] or ENCODERS[] below
 * 3. Wire the hardware and update the pin numbers
 *
 * To remove a control:
 * Simply remove its entry from the array below.
 */

#pragma once

#include <cstddef>

#include "InputID.hpp"
#include "core/Type.hpp"
#include "core/struct/Button.hpp"
#include "core/struct/Encoder.hpp"

namespace Config {

/*
 * Button definitions
 *
 * All physical buttons on the device.
 * Organized by function: navigation, encoder buttons, etc.
 */
constexpr Hardware::Button BUTTONS[] = {
    /* Navigation buttons (left side) */
    {ButtonID::LEFT_TOP, muxPin(9)},
    {ButtonID::LEFT_CENTER, muxPin(10)},
    {ButtonID::LEFT_BOTTOM, muxPin(11)},

    /* Navigation buttons (bottom) */
    {ButtonID::BOTTOM_LEFT, muxPin(14)},
    {ButtonID::BOTTOM_CENTER, muxPin(13)},
    {ButtonID::BOTTOM_RIGHT, muxPin(12)},

    /* Encoder buttons */
    {ButtonID::NAV, mcuPin(32)},

    /* Encoder macros buttons */
    {ButtonID::MACRO_1, muxPin(7)},
    {ButtonID::MACRO_2, muxPin(4)},
    {ButtonID::MACRO_3, muxPin(2)},
    {ButtonID::MACRO_4, muxPin(0)},
    {ButtonID::MACRO_5, muxPin(6)},
    {ButtonID::MACRO_6, muxPin(5)},
    {ButtonID::MACRO_7, muxPin(3)},
    {ButtonID::MACRO_8, muxPin(1)},
};

/*
 * Encoder definitions
 *
 * All physical rotary encoders on the device.
 *
 * Mode Absolute (default): Normalized value [0.0 → 1.0] with software stops
 * Mode Relative: Infinite rotation, emits ±1.0 delta per physical detent
 *
 * Format: {id, pinA, pinB, ppr, stepsPerDetent, mode}
 */
constexpr Hardware::Encoder ENCODERS[] = {
    /* Main encoder matrix (2x4 grid) - Absolute mode for parameter control */
    {EncoderID::MACRO_1, mcuPin(22), mcuPin(23)},
    {EncoderID::MACRO_2, mcuPin(18), mcuPin(19)},
    {EncoderID::MACRO_3, mcuPin(40), mcuPin(41)},
    {EncoderID::MACRO_4, mcuPin(36), mcuPin(37)},
    {EncoderID::MACRO_5, mcuPin(20), mcuPin(21)},
    {EncoderID::MACRO_6, mcuPin(16), mcuPin(17)},
    {EncoderID::MACRO_7, mcuPin(14), mcuPin(15)},
    {EncoderID::MACRO_8, mcuPin(38), mcuPin(39)},

    /* Navigation encoder - Relative mode (infinite rotation, ±1.0 per detent) */
    {EncoderID::NAV, mcuPin(31), mcuPin(30), 24, 4, Hardware::EncoderMode::Relative},

    /* Optional encoder - Absolute mode (high precision parameter control) */
    {EncoderID::OPT, mcuPin(34), mcuPin(33), 600, 1}};

/*
 * Computed array sizes (compile-time constants)
 */
constexpr size_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
constexpr size_t ENCODER_COUNT = sizeof(ENCODERS) / sizeof(ENCODERS[0]);

}  // namespace Config
