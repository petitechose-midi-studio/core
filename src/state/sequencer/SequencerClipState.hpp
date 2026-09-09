#pragma once

#include <cstdint>

#include <oc/note/clock/ClockConstants.hpp>

namespace core::state::sequencer {

/** Playback placement owned by the single implicit Clip of one Track. */
struct SequencerClipState {
    static constexpr uint16_t DEFAULT_END_TICK = static_cast<uint16_t>(
        8U * oc::note::clock::PPQN / 4U
    );

    uint16_t playStartTick = 0U;
    uint16_t loopStartTick = 0U;
    uint16_t loopEndTick = DEFAULT_END_TICK;

    void reset() noexcept { *this = SequencerClipState{}; }
};

static_assert(
    sizeof(SequencerClipState) == 6U,
    "An implicit Clip must remain three musical-tick boundaries"
);

}  // namespace core::state::sequencer
