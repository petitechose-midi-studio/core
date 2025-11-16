#pragma once

#include "../Type.hpp"

namespace Hardware {

/*
 * EncoderMode - Defines encoder behavior
 *
 * Absolute: Normalized value [0.0 → 1.0] with software stops (parameter control)
 * Relative: Infinite rotation, emits cumulative position (menu navigation)
 */
enum class EncoderMode : uint8_t {
    Absolute,  // Butées logicielles, valeur normalisée
    Relative   // Infini, position cumulative (±1.0 par cran)
};

/*
 * Hardware encoder setup definition
 *
 * Represents the initial hardware configuration for an encoder.
 * This is a static definition used during initialization.
 *
 * Fields:
 * - id: Unique encoder identifier
 * - pinA, pinB: GPIO pins for quadrature encoding
 * - ppr: Pulses per revolution (mechanical spec)
 * - stepsPerDetent: Hardware pulses per physical detent (mode full = x4)
 * - mode: Absolute (parameter) or Relative (navigation)
 */
struct Encoder {
    EncoderID id;
    GpioPin pinA;
    GpioPin pinB;
    uint16_t ppr = 24;
    uint8_t stepsPerDetent = 1;
    EncoderMode mode = EncoderMode::Absolute;
};

}  // namespace Hardware
