#pragma once

#include <cstdint>

namespace Boot {

enum class Phase : uint8_t {
    NOT_STARTED = 0,
    HARDWARE_INIT,
    DISPLAY_INIT,
    MINIMAL_UI,
    LOADING_FONTS,
    INPUT_INIT,
    MIDI_INIT,
    READY
};

struct Status {
    Phase currentPhase = Phase::NOT_STARTED;
    uint8_t progress = 0;
    const char* text = "";

    bool isComplete() const { return currentPhase == Phase::READY; }
};

} // namespace Boot
