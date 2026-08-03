#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/StepPresetFileStore.hpp"
#include "state/project/ProjectState.hpp"
#include "state/sequencer/SequencerGraphAsset.hpp"
#include "state/sequencer/SequencerStepPresetModel.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"

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
    ALLOCATION_UNAVAILABLE,
    HISTORY_UNAVAILABLE,
    EMPTY,
    INCOMPATIBLE,
    CAPACITY,
    CORRUPT,
    UNSUPPORTED_VERSION,
    STALE_TARGET,
    COLLISION,
    QUEUED,
    FAILED,
};

struct SequencerStepPresetListResult {
    SequencerStepPresetStatus status = SequencerStepPresetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint8_t count = 0;
    bool truncated = false;
    bool hasPrevious = false;
    bool hasNext = false;
    uint16_t totalCount = 0;

    bool ok() const {
        return status == SequencerStepPresetStatus::OK;
    }
};

struct SequencerStepPresetInspectResult {
    SequencerStepPresetStatus status = SequencerStepPresetStatus::OK;
    core::state::sequencer::SequencerGraphAssetStatus assetStatus =
        core::state::sequencer::SequencerGraphAssetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint16_t bytes = 0;
    core::state::sequencer::SequencerStepPresetDescriptor descriptor{};

    bool inspected() const {
        return descriptor.valid;
    }
};

enum class SequencerStepPresetActivation : uint8_t {
    NONE = 0,
    APPLIED,
    QUEUED,
};

struct SequencerStepPresetActionResult {
    SequencerStepPresetStatus status = SequencerStepPresetStatus::OK;
    core::state::sequencer::SequencerGraphAssetStatus assetStatus =
        core::state::sequencer::SequencerGraphAssetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint16_t bytes = 0;
    SequencerStepPresetActivation activation =
        SequencerStepPresetActivation::NONE;
    uint32_t activationGeneration = 0;
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};

    bool ok() const {
        return (status == SequencerStepPresetStatus::OK ||
                status == SequencerStepPresetStatus::QUEUED) &&
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

    SequencerStepPresetListResult listPresetsPage(
        Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        core::persistence::StepPresetFilePageDirection direction
    ) const;
    SequencerStepPresetActionResult nextPresetId(char* out, size_t outSize) const;
    core::state::sequencer::SequencerStepPresetTarget captureTarget() const;
    bool targetMatches(
        const core::state::sequencer::SequencerStepPresetTarget& target
    ) const;
    uint32_t projectRevision() const;
    SequencerStepPresetInspectResult inspectPreset(
        const char* presetId,
        const core::state::sequencer::SequencerStepPresetTarget& target,
        uint8_t previewStateIndex,
        uint32_t generation
    ) const;
    SequencerStepPresetActionResult savePreset(
        const char* presetId,
        const core::state::sequencer::SequencerStepPresetTarget& target,
        bool allowOverwrite
    ) const;
    SequencerStepPresetActionResult applyPreset(
        const char* presetId,
        const core::state::sequencer::SequencerStepPresetTarget& target,
        const core::state::sequencer::SequencerStepPresetPreviewKey& expectedPreview
    ) const;
    core::state::sequencer::SequencerTrackActivationStatus activationStatus(
        uint8_t trackIndex,
        uint32_t generation
    ) const;
    SequencerStepPresetActionResult renamePreset(
        const char* presetId,
        const char* expectedSemanticName,
        const char* newSemanticName
    ) const;
    SequencerStepPresetActionResult deletePreset(
        const char* presetId,
        const char* expectedSemanticName
    ) const;

private:
    SequencerStepPresetInspectResult inspectPresetPrepared(
        const char* presetId,
        const core::state::sequencer::SequencerStepPresetTarget& target,
        uint8_t previewStateIndex,
        uint32_t generation,
        uint8_t* encodedWorkspace,
        core::state::sequencer::SequencerStepGraphPreset* preparedPreset
    ) const;

    core::state::CoreState* state_ = nullptr;
    core::persistence::ProductFileService* files_ = nullptr;
};

}  // namespace core::handler
