#pragma once

#include <cstdint>

#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/StepProperty.hpp"

namespace core::state::sequencer {

inline constexpr int SEQUENCER_BASE_STEP_PROPERTY_COUNT =
    static_cast<int>(StepProperty::PROBABILITY) + 1;

[[nodiscard]] inline int sequencerPropertySelectionItemCount(
    const SequencerCcLaneBank* bank
) {
    const uint8_t laneCount = bank ? sequencerCcLaneCount(*bank) : 0U;
    return SEQUENCER_BASE_STEP_PROPERTY_COUNT + laneCount +
        (laneCount < SequencerCcLaneBank::MAX_LANES ? 1 : 0);
}

[[nodiscard]] inline int8_t sequencerPropertySelectionLaneAt(
    const SequencerCcLaneBank* bank,
    int itemIndex
) {
    if (bank == nullptr || itemIndex < SEQUENCER_BASE_STEP_PROPERTY_COUNT) {
        return -1;
    }
    const int denseTarget = itemIndex - SEQUENCER_BASE_STEP_PROPERTY_COUNT;
    int dense = 0;
    for (uint8_t lane = 0; lane < bank->lanes.size(); ++lane) {
        if (!bank->lanes[lane].occupied) continue;
        if (dense == denseTarget) return static_cast<int8_t>(lane);
        ++dense;
    }
    return -1;
}

[[nodiscard]] inline int sequencerPropertySelectionIndexForLane(
    const SequencerCcLaneBank* bank,
    uint8_t targetLane
) {
    if (bank == nullptr || targetLane >= bank->lanes.size() ||
        !bank->lanes[targetLane].occupied) {
        return -1;
    }
    int index = SEQUENCER_BASE_STEP_PROPERTY_COUNT;
    for (uint8_t lane = 0; lane < targetLane; ++lane) {
        if (bank->lanes[lane].occupied) ++index;
    }
    return index;
}

[[nodiscard]] inline bool sequencerPropertySelectionIsAdd(
    const SequencerCcLaneBank* bank,
    int itemIndex
) {
    const uint8_t laneCount = bank ? sequencerCcLaneCount(*bank) : 0U;
    return laneCount < SequencerCcLaneBank::MAX_LANES &&
           itemIndex == SEQUENCER_BASE_STEP_PROPERTY_COUNT + laneCount;
}

}  // namespace core::state::sequencer
