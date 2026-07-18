#pragma once

#include <cstdint>

#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/StepProperty.hpp"

namespace core::state::sequencer {

inline constexpr int SEQUENCER_STEP_STATE_SELECTION_INDEX = 0;
inline constexpr int SEQUENCER_STEP_CHANCE_SELECTION_INDEX = 1;
inline constexpr int SEQUENCER_STEP_NOTE_SELECTION_INDEX = 2;
inline constexpr int SEQUENCER_STEP_VELOCITY_SELECTION_INDEX = 3;
inline constexpr int SEQUENCER_STEP_GATE_SELECTION_INDEX = 4;
inline constexpr int SEQUENCER_STEP_NUDGE_SELECTION_INDEX = 5;
inline constexpr int SEQUENCER_BASE_STEP_PROPERTY_COUNT = 6;

[[nodiscard]] inline int sequencerPropertySelectionIndexForProperty(
    StepProperty property
) {
    switch (property) {
        case StepProperty::PROBABILITY:
            return SEQUENCER_STEP_CHANCE_SELECTION_INDEX;
        case StepProperty::VELOCITY:
            return SEQUENCER_STEP_VELOCITY_SELECTION_INDEX;
        case StepProperty::GATE:
            return SEQUENCER_STEP_GATE_SELECTION_INDEX;
        case StepProperty::NUDGE:
            return SEQUENCER_STEP_NUDGE_SELECTION_INDEX;
        case StepProperty::NOTE:
        default:
            return SEQUENCER_STEP_NOTE_SELECTION_INDEX;
    }
}

[[nodiscard]] inline bool sequencerPropertySelectionIsState(int itemIndex) {
    return itemIndex == SEQUENCER_STEP_STATE_SELECTION_INDEX;
}

[[nodiscard]] inline bool sequencerPropertySelectionIsBaseProperty(int itemIndex) {
    return itemIndex > SEQUENCER_STEP_STATE_SELECTION_INDEX &&
           itemIndex < SEQUENCER_BASE_STEP_PROPERTY_COUNT;
}

[[nodiscard]] inline StepProperty sequencerPropertySelectionPropertyAt(
    int itemIndex
) {
    switch (itemIndex) {
        case SEQUENCER_STEP_CHANCE_SELECTION_INDEX:
            return StepProperty::PROBABILITY;
        case SEQUENCER_STEP_VELOCITY_SELECTION_INDEX:
            return StepProperty::VELOCITY;
        case SEQUENCER_STEP_GATE_SELECTION_INDEX:
            return StepProperty::GATE;
        case SEQUENCER_STEP_NUDGE_SELECTION_INDEX:
            return StepProperty::NUDGE;
        case SEQUENCER_STEP_NOTE_SELECTION_INDEX:
        default:
            return StepProperty::NOTE;
    }
}

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
