#pragma once

#include <algorithm>
#include <cstdint>

#include <oc/note/clock/ClockConstants.hpp>

namespace core::sequencer {

constexpr uint32_t MAX_REALTIME_CLOCK_BURST_PER_UPDATE = 96;

inline uint32_t tickPeriodUsForTempo(float tempo) {
    if (!(tempo > 0.0f)) {
        tempo = 120.0f;
    }

    constexpr float ppqn = static_cast<float>(oc::note::clock::PPQN);
    const float periodUs = 60000000.0f / (tempo * ppqn);
    return std::max<uint32_t>(1, static_cast<uint32_t>(periodUs + 0.5f));
}

}  // namespace core::sequencer
