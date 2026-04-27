#include "ClockSourceSelector.hpp"

#include <algorithm>

namespace core::sequencer {

namespace {
constexpr uint16_t MIN_AUTO_FALLBACK_MS = 100;
constexpr uint16_t MAX_AUTO_FALLBACK_MS = 5000;
constexpr uint8_t MIN_AUTO_LOCK_CLOCKS = 1;
constexpr uint8_t MAX_AUTO_LOCK_CLOCKS = 96;
}  // namespace

void ClockSourceSelector::recordClock(core::state::MidiSyncMode mode, uint8_t autoLockClockCount) {
    if (clock_streak_ < 255) {
        clock_streak_ += 1;
    }

    if (mode == core::state::MidiSyncMode::SLAVE ||
        clock_streak_ >= lockClockCount_(autoLockClockCount)) {
        locked_ = true;
    }
}

ClockSourceSelector::UpdateResult ClockSourceSelector::update(
    core::state::MidiSyncMode mode,
    uint16_t autoFallbackMs,
    uint32_t nowMs,
    uint32_t lastExternalClockMs
) {
    bool resetExternalTempo = false;

    if (mode == core::state::MidiSyncMode::MASTER) {
        locked_ = false;
        clock_streak_ = 0;
        resetExternalTempo = true;
    } else if (mode == core::state::MidiSyncMode::AUTO) {
        const uint32_t elapsed = nowMs - lastExternalClockMs;
        if (locked_ && elapsed > fallbackMs_(autoFallbackMs)) {
            locked_ = false;
            clock_streak_ = 0;
            resetExternalTempo = true;
        }
    } else {
        locked_ = true;
    }

    const bool externalSignal = hasExternalClockSignal(autoFallbackMs, nowMs, lastExternalClockMs);
    if (!externalSignal) {
        resetExternalTempo = true;
    }

    const bool useExternal =
        mode == core::state::MidiSyncMode::SLAVE ||
        (mode == core::state::MidiSyncMode::AUTO && locked_);

    return {
        .externalSignal = externalSignal,
        .useExternal = useExternal,
        .resetExternalTempo = resetExternalTempo,
    };
}

bool ClockSourceSelector::hasExternalClockSignal(uint16_t autoFallbackMs,
                                                 uint32_t nowMs,
                                                 uint32_t lastExternalClockMs) const {
    if (lastExternalClockMs == 0) return false;
    return (nowMs - lastExternalClockMs) <= fallbackMs_(autoFallbackMs);
}

uint8_t ClockSourceSelector::lockClockCount_(uint8_t autoLockClockCount) const {
    return std::clamp(autoLockClockCount, MIN_AUTO_LOCK_CLOCKS, MAX_AUTO_LOCK_CLOCKS);
}

uint16_t ClockSourceSelector::fallbackMs_(uint16_t autoFallbackMs) const {
    return std::clamp(autoFallbackMs, MIN_AUTO_FALLBACK_MS, MAX_AUTO_FALLBACK_MS);
}

}  // namespace core::sequencer
