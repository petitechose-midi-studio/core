#pragma once

#include <cstdint>

#include "state/sequencer/SequencerGraphAssetCodec.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

struct SequencerGraphPresetWorkflowResult {
    SequencerGraphAssetStatus status = SequencerGraphAssetStatus::OK;
    SequencerGraphAssetReport report{};
    uint16_t bytesWritten = 0;

    bool ok() const { return status == SequencerGraphAssetStatus::OK; }
};

SequencerGraphPresetWorkflowResult saveFocusedStepGraphPreset(
    const SequencerState& sequencer,
    uint8_t* out,
    uint16_t capacity
);

SequencerGraphPresetWorkflowResult loadFocusedStepGraphPreset(
    SequencerState& sequencer,
    const uint8_t* data,
    uint16_t size
);

}  // namespace core::state::sequencer
