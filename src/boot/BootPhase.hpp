#pragma once

#include <cstdint>

namespace Boot {

enum class Phase : uint8_t {
    NotStarted = 0,
    HardwareInit,
    DisplayInit,
    MinimalUI,
    LoadingFonts,
    InputInit,
    MidiInit,
    Ready
};

struct Status {
    Phase currentPhase = Phase::NotStarted;
    uint8_t progress = 0;
    const char* text = "";

    bool isComplete() const { return currentPhase == Phase::Ready; }
};

} // namespace Boot
