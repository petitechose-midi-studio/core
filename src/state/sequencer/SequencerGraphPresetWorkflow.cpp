#include "state/sequencer/SequencerGraphPresetWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"

namespace core::state::sequencer {

FLASHMEM SequencerGraphPresetWorkflowResult saveFocusedStepGraphPreset(
    const SequencerState& sequencer,
    uint8_t* out,
    uint16_t capacity
) {
    SequencerGraphPresetWorkflowResult result{};

    auto preset = core::app::makeExtmemUnique<SequencerStepGraphPreset>();
    if (!preset) {
        result.status = SequencerGraphAssetStatus::RESOURCE_EXHAUSTED;
        result.report.status = result.status;
        return result;
    }
    if (!captureStepGraphPreset(
            sequencer,
            sequencer.focusedStep.get(),
            *preset,
            &result.report
        )) {
        result.status = result.report.status;
        return result;
    }

    const auto encoded = encodeStepGraphPreset(*preset, out, capacity);
    result.status = encoded.status;
    result.bytesWritten = encoded.bytesWritten;
    return result;
}

FLASHMEM SequencerGraphPresetWorkflowResult loadFocusedStepGraphPreset(
    SequencerState& sequencer,
    const uint8_t* data,
    uint16_t size
) {
    SequencerGraphPresetWorkflowResult result{};

    auto preset = core::app::makeExtmemUnique<SequencerStepGraphPreset>();
    if (!preset) {
        result.status = SequencerGraphAssetStatus::RESOURCE_EXHAUSTED;
        result.report.status = result.status;
        return result;
    }
    if (!decodeStepGraphPreset(data, size, *preset, &result.report)) {
        result.status = result.report.status;
        return result;
    }
    if (!applyStepGraphPreset(
            sequencer,
            sequencer.focusedStep.get(),
            *preset,
            &result.report
        )) {
        result.status = result.report.status;
        return result;
    }

    result.status = result.report.status;
    return result;
}

}  // namespace core::state::sequencer
