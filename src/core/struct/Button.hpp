#pragma once

#include "../Type.hpp"

namespace Hardware {

/*
 * Hardware button setup definition
 *
 * Represents the initial hardware configuration for a button.
 * This is a static definition used during initialization.
 */
struct Button {
    ButtonID id;
    GpioPin pin;
};

}  // namespace Hardware
