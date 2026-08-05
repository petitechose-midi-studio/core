#pragma once

#include <cstdint>

#include "state/MidiSyncState.hpp"

namespace core::sequencer {

/**
 * Small state machine for MIDI clock source ownership.
 *
 * MASTER always selects internal transport, SLAVE always selects external, and
 * AUTO locks to external after enough clocks then falls back after silence. This
 * class decides source only; tempo estimation and UI projection live elsewhere.
 */
class ClockSourceSelector {
public:
    struct UpdateResult {
        bool externalSignal = false;
        bool useExternal = false;
        bool resetExternalTempo = false;
    };

    void recordClock(core::state::MidiSyncMode mode,
                     uint8_t autoLockClockCount,
                     uint16_t autoFallbackMs,
                     uint32_t nowMs,
                     uint32_t lastExternalClockMs);
    UpdateResult update(core::state::MidiSyncMode mode,
                        uint16_t autoFallbackMs,
                        uint32_t nowMs,
                        uint32_t lastExternalClockMs);

    bool locked() const { return locked_; }
    bool hasExternalClockSignal(uint16_t autoFallbackMs,
                                uint32_t nowMs,
                                uint32_t lastExternalClockMs) const;

private:
    uint8_t lockClockCount_(uint8_t autoLockClockCount) const;
    uint16_t fallbackMs_(uint16_t autoFallbackMs) const;
    bool externalClockTimedOut_(uint16_t autoFallbackMs,
                                uint32_t nowMs,
                                uint32_t lastExternalClockMs) const;

    uint8_t clock_streak_ = 0;
    bool locked_ = false;
};

}  // namespace core::sequencer
