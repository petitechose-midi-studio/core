#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/type/Result.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/ProductFileService.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerGraphPresetWorkflow.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::handler {

namespace {

using core::state::sequencer::STEP_GRAPH_PRESET_MAX_ENCODED_SIZE;

struct StepPresetBuffer {
    uint8_t bytes[STEP_GRAPH_PRESET_MAX_ENCODED_SIZE]{};
};

FLASHMEM core::app::ExtmemUniquePtr<StepPresetBuffer> makeStepPresetBuffer() {
    return core::app::makeExtmemUnique<StepPresetBuffer>();
}

FLASHMEM SequencerStepPresetStatus statusFromFileError(oc::type::ErrorCode code) {
    switch (code) {
        case oc::type::ErrorCode::RESOURCE_NOT_FOUND:
            return SequencerStepPresetStatus::EMPTY;
        case oc::type::ErrorCode::INVALID_STATE:
        case oc::type::ErrorCode::HARDWARE_NOT_FOUND:
        case oc::type::ErrorCode::HARDWARE_INIT_FAILED:
            return SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        default:
            return SequencerStepPresetStatus::FAILED;
    }
}

FLASHMEM SequencerStepPresetStatus statusFromAsset(
    core::state::sequencer::SequencerGraphAssetStatus status
) {
    return status == core::state::sequencer::SequencerGraphAssetStatus::INCOMPATIBLE_TARGET
        ? SequencerStepPresetStatus::INCOMPATIBLE
        : SequencerStepPresetStatus::FAILED;
}

FLASHMEM void copyPresetId(
    char* target,
    size_t targetSize,
    const char* source
) {
    if (target == nullptr || targetSize == 0) return;
    const char* id = source ? source : "";
    std::strncpy(target, id, targetSize - 1U);
    target[targetSize - 1U] = '\0';
}

}  // namespace

FLASHMEM SequencerStepPresetDomainServices::SequencerStepPresetDomainServices(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files
)
    : state_(&state)
    , files_(&files) {}

FLASHMEM SequencerStepPresetDomainServices
SequencerStepPresetDomainServices::fromCoreState(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files
) {
    return SequencerStepPresetDomainServices{state, files};
}

FLASHMEM SequencerStepPresetListResult SequencerStepPresetDomainServices::listPresets(
    Entry* entries,
    uint8_t capacity
) const {
    SequencerStepPresetListResult result{};
    if (files_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_);
    auto listed = store.list(entries, capacity);
    if (!listed) {
        result.status = statusFromFileError(listed.error().code);
        result.fileError = listed.error().code;
        return result;
    }

    result.count = listed.value().count;
    result.truncated = listed.value().truncated;
    return result;
}

FLASHMEM SequencerStepPresetActionResult SequencerStepPresetDomainServices::nextPresetId(
    char* out,
    size_t outSize
) const {
    SequencerStepPresetActionResult result{};
    if (files_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_);
    auto next = store.nextPresetId(out, outSize);
    if (!next) {
        result.status = statusFromFileError(next.error().code);
        result.fileError = next.error().code;
        return result;
    }
    copyPresetId(result.presetId, sizeof(result.presetId), out);
    return result;
}

FLASHMEM SequencerStepPresetActionResult SequencerStepPresetDomainServices::savePreset(
    const char* presetId
) const {
    SequencerStepPresetActionResult result{};
    copyPresetId(result.presetId, sizeof(result.presetId), presetId);
    if (state_ == nullptr || files_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }

    auto buffer = makeStepPresetBuffer();
    if (!buffer) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        return result;
    }

    const auto encoded = core::state::sequencer::saveFocusedStepGraphPreset(
        state_->sequencer,
        buffer->bytes,
        STEP_GRAPH_PRESET_MAX_ENCODED_SIZE
    );
    result.assetStatus = encoded.status;
    result.bytes = encoded.bytesWritten;
    if (!encoded.ok()) {
        result.status = statusFromAsset(encoded.status);
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_);
    auto saved = store.save(presetId, buffer->bytes, encoded.bytesWritten);
    if (!saved) {
        result.status = statusFromFileError(saved.error().code);
        result.fileError = saved.error().code;
        return result;
    }

    result.bytes = static_cast<uint16_t>(saved.value().bytesWritten);
    copyPresetId(result.presetId, sizeof(result.presetId), saved.value().presetId);
    return result;
}

FLASHMEM SequencerStepPresetActionResult SequencerStepPresetDomainServices::loadPreset(
    const char* presetId
) const {
    SequencerStepPresetActionResult result{};
    copyPresetId(result.presetId, sizeof(result.presetId), presetId);
    if (state_ == nullptr || files_ == nullptr) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }

    auto buffer = makeStepPresetBuffer();
    if (!buffer) {
        result.status = SequencerStepPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::RESOURCE_EXHAUSTED;
        return result;
    }

    core::persistence::StepPresetFileStore store(*files_);
    uint16_t payloadSize = 0;
    auto loaded = store.load(
        presetId,
        buffer->bytes,
        STEP_GRAPH_PRESET_MAX_ENCODED_SIZE,
        payloadSize
    );
    if (!loaded) {
        result.status = statusFromFileError(loaded.error().code);
        result.fileError = loaded.error().code;
        return result;
    }
    result.bytes = payloadSize;

    core::state::sequencer::SequencerHistoryPatternSnapshot before{};
    const bool beforeCaptured =
        core::state::sequencer::captureHistorySnapshot(state_->sequencer, before);

    const auto applied = core::state::sequencer::loadFocusedStepGraphPreset(
        state_->sequencer,
        buffer->bytes,
        payloadSize
    );
    result.assetStatus = applied.status;
    if (!applied.ok()) {
        result.status = statusFromAsset(applied.status);
        return result;
    }

    if (beforeCaptured) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after{};
        if (core::state::sequencer::captureHistorySnapshot(state_->sequencer, after) &&
            !core::state::sequencer::sameMusicalHistorySnapshot(before, after)) {
            state_->recordSequencerPatternHistory(
                std::move(before),
                std::move(after),
                core::state::sequencer::SequencerHistoryDescriptor{
                    .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
                    .stepIndex = state_->sequencer.focusedStep.get(),
                    .property = core::state::sequencer::StepProperty::NOTE,
                    .hasValue = false,
                }
            );
        }
    } else {
        state_->markSequencerProjectMutated();
    }

    return result;
}

FLASHMEM const char* sequencerStepPresetStatusLabel(SequencerStepPresetStatus status) {
    switch (status) {
        case SequencerStepPresetStatus::OK:
            return "OK";
        case SequencerStepPresetStatus::STORAGE_UNAVAILABLE:
            return "STORAGE_UNAVAILABLE";
        case SequencerStepPresetStatus::EMPTY:
            return "EMPTY";
        case SequencerStepPresetStatus::INCOMPATIBLE:
            return "INCOMPATIBLE";
        case SequencerStepPresetStatus::FAILED:
        default:
            return "FAILED";
    }
}

}  // namespace core::handler
