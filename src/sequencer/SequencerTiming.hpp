#pragma once

#include <algorithm>
#include <cstdint>

#include <oc/note/clock/ClockConstants.hpp>

namespace core::sequencer {

/**
 * Realtime clock burst clamp shared by loop and timer runtime lanes.
 *
 * This limits catch-up MIDI clock emission after stalls so one delayed update
 * cannot monopolize the loop or timer callback.
 */
constexpr uint32_t MAX_REALTIME_CLOCK_BURST_PER_UPDATE = 96;

/**
 * Convert BPM to one PPQN tick period in microseconds.
 *
 * Invalid/non-positive tempos fall back to 120 BPM. Callers use this value to
 * translate sequencer ticks into realtime MIDI deadlines.
 */
inline uint32_t tickPeriodUsForTempo(float tempo) {
    if (!(tempo > 0.0f)) {
        tempo = 120.0f;
    }

    constexpr float ppqn = static_cast<float>(oc::note::clock::PPQN);
    const float periodUs = 60000000.0f / (tempo * ppqn);
    return std::max<uint32_t>(1, static_cast<uint32_t>(periodUs + 0.5f));
}

}  // namespace core::sequencer
