#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/StepPresetFileStore.hpp"
#include "state/project/ProjectState.hpp"
#include "state/sequencer/SequencerGraphAssetCodec.hpp"

namespace core::persistence {
class ProductFileService;
}

namespace core::state {
struct CoreState;
}

namespace core::handler {

enum class SequencerStepPresetStatus : uint8_t {
    OK = 0,
    STORAGE_UNAVAILABLE,
    EMPTY,
    INCOMPATIBLE,
    FAILED,
};

struct SequencerStepPresetListResult {
    SequencerStepPresetStatus status = SequencerStepPresetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint8_t count = 0;
    bool truncated = false;

    bool ok() const {
        return status == SequencerStepPresetStatus::OK;
    }
};

struct SequencerStepPresetActionResult {
    SequencerStepPresetStatus status = SequencerStepPresetStatus::OK;
    core::state::sequencer::SequencerGraphAssetStatus assetStatus =
        core::state::sequencer::SequencerGraphAssetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint16_t bytes = 0;
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};

    bool ok() const {
        return status == SequencerStepPresetStatus::OK &&
               assetStatus == core::state::sequencer::SequencerGraphAssetStatus::OK &&
               fileError == oc::type::ErrorCode::OK;
    }
};

class SequencerStepPresetDomainServices {
public:
    using Entry = core::persistence::StepPresetFileListEntry;

    SequencerStepPresetDomainServices() = default;
    SequencerStepPresetDomainServices(core::state::CoreState& state,
                                      core::persistence::ProductFileService& files);

    static SequencerStepPresetDomainServices fromCoreState(
        core::state::CoreState& state,
        core::persistence::ProductFileService& files
    );

    SequencerStepPresetListResult listPresets(Entry* entries, uint8_t capacity) const;
    SequencerStepPresetActionResult nextPresetId(char* out, size_t outSize) const;
    SequencerStepPresetActionResult savePreset(const char* presetId) const;
    SequencerStepPresetActionResult loadPreset(const char* presetId) const;

private:
    core::state::CoreState* state_ = nullptr;
    core::persistence::ProductFileService* files_ = nullptr;
};

const char* sequencerStepPresetStatusLabel(SequencerStepPresetStatus status);

}  // namespace core::handler
