#pragma once

#include <cstdint>

#include "state/modulation/ProjectModulationState.hpp"

namespace core::ui::modulation::lfo {

inline const char* shapeLabel(
    core::state::modulation::ModulatorLfoShape shape
) {
    using Shape = core::state::modulation::ModulatorLfoShape;
    switch (shape) {
        case Shape::TRIANGLE: return "Triangle";
        case Shape::SAW_UP: return "Saw Up";
        case Shape::SAW_DOWN: return "Saw Down";
        case Shape::SQUARE: return "Square";
        case Shape::SINE:
        default: return "Sine";
    }
}

inline const char* rateLabel(uint8_t index) {
    if (index == 0U) return "1/64 Sync";
    if (index == 1U) return "1/32 Sync";
    if (index == 2U) return "1/16 Sync";
    if (index == 3U) return "1/8 Sync";
    if (index == 4U) return "1/4 Sync";
    if (index == 5U) return "1/2 Sync";
    if (index == 6U) return "1 Bar Sync";
    if (index == 7U) return "2 Bars Sync";
    if (index == 8U) return "4 Bars Sync";
    if (index == 9U) return "8 Bars Sync";
    if (index == 10U) return "16 Bars Sync";
    return "32 Bars Sync";
}

inline const char* rateCompactLabel(uint8_t index) {
    if (index == 0U) return "1/64";
    if (index == 1U) return "1/32";
    if (index == 2U) return "1/16";
    if (index == 3U) return "1/8";
    if (index == 4U) return "1/4";
    if (index == 5U) return "1/2";
    if (index == 6U) return "1B";
    if (index == 7U) return "2B";
    if (index == 8U) return "4B";
    if (index == 9U) return "8B";
    if (index == 10U) return "16B";
    return "32B";
}

}  // namespace core::ui::modulation::lfo
