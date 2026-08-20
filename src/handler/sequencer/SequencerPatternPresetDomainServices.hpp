#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/PatternPresetFileStore.hpp"
#include "state/project/ProjectState.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerPatternPreset.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"

namespace core::persistence {
class ProductDirectoryCatalog;
class ProductFileService;
}

namespace core::state {
struct CoreState;
}

namespace core::handler {

enum class SequencerPatternPresetDomainStatus : uint8_t {
    OK = 0,
    STORAGE_UNAVAILABLE,
    ALLOCATION_UNAVAILABLE,
    HISTORY_UNAVAILABLE,
    EMPTY,
    INCOMPATIBLE,
    CORRUPT,
    UNSUPPORTED_VERSION,
    STALE_TARGET,
    COLLISION,
    READ_ONLY,
    QUEUED,
    FAILED,
};

struct SequencerPatternPresetListResult {
    SequencerPatternPresetDomainStatus status =
        SequencerPatternPresetDomainStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint8_t count = 0U;
    bool truncated = false;
    bool hasPrevious = false;
    bool hasNext = false;
    uint16_t totalCount = 0U;

    [[nodiscard]] bool ok() const {
        return status == SequencerPatternPresetDomainStatus::OK;
    }
};

struct SequencerPatternPresetInspectResult {
    SequencerPatternPresetDomainStatus status =
        SequencerPatternPresetDomainStatus::OK;
    core::state::sequencer::SequencerPatternPresetStatus codecStatus =
        core::state::sequencer::SequencerPatternPresetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint16_t bytes = 0U;
    core::state::sequencer::SequencerPatternPresetDescriptor descriptor{};

    [[nodiscard]] bool inspected() const { return descriptor.valid; }
};

enum class SequencerPatternPresetActivation : uint8_t {
    NONE = 0,
    APPLIED,
    QUEUED,
};

struct SequencerPatternPresetActionResult {
    SequencerPatternPresetDomainStatus status =
        SequencerPatternPresetDomainStatus::OK;
    core::state::sequencer::SequencerPatternPresetStatus codecStatus =
        core::state::sequencer::SequencerPatternPresetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint16_t bytes = 0U;
    SequencerPatternPresetActivation activation =
        SequencerPatternPresetActivation::NONE;
    uint32_t activationGeneration = 0U;
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    char semanticName[
        core::state::sequencer::SEQUENCER_PRESET_SEMANTIC_NAME_SIZE
    ] = {};

    [[nodiscard]] bool ok() const {
        return (status == SequencerPatternPresetDomainStatus::OK ||
                status == SequencerPatternPresetDomainStatus::QUEUED) &&
            codecStatus ==
                core::state::sequencer::SequencerPatternPresetStatus::OK &&
            fileError == oc::type::ErrorCode::OK;
    }
};

/**
 * One provisional Pattern-preset transaction.
 *
 * All fallible snapshot, history and runtime-activation work is admitted
 * before the candidate becomes live. Confirm transfers the retained change
 * into Undo exactly once; Cancel restores the captured state without adding
 * an Undo/Redo entry or advancing the Project revision.
 */
struct SequencerPatternPresetPreviewSession {
    core::state::sequencer::SequencerPatternPresetTarget target{};
    core::state::sequencer::SequencerHistoryPatternChangePtr pattern{};
    core::state::sequencer::SequencerHistoryDrumChangePtr drum{};
    core::state::sequencer::SequencerPreparedActiveTrackSynchronization
        rollbackPatternBank{};
    core::state::sequencer::SequencerHistoryGraphPtr rollbackDrumBankGraph{};
    core::state::sequencer::SequencerTrackActivationHistoryPlan activation{};
    SequencerPatternPresetActivation presentationActivation =
        SequencerPatternPresetActivation::NONE;
    uint32_t activationGeneration = 0U;

    [[nodiscard]] bool active() const {
        return target.valid && (pattern != nullptr || drum != nullptr) &&
            activation.valid();
    }
    void reset();
};

class SequencerPatternPresetDomainServices {
public:
    using Entry = core::persistence::PatternPresetFileListEntry;

    SequencerPatternPresetDomainServices() = default;
    SequencerPatternPresetDomainServices(
        core::state::CoreState& state,
        core::persistence::ProductFileService& files,
        core::persistence::ProductDirectoryCatalog& catalog
    );

    static SequencerPatternPresetDomainServices fromCoreState(
        core::state::CoreState& state,
        core::persistence::ProductFileService& files,
        core::persistence::ProductDirectoryCatalog& catalog
    );

    SequencerPatternPresetListResult listPresetsPage(
        Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        core::persistence::PatternPresetFilePageDirection direction,
        core::state::sequencer::SequencerPatternPresetSourceFilter filter,
        core::state::sequencer::SequencerTrackKind trackKind,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;
    SequencerPatternPresetListResult listFoldersPage(
        Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        core::persistence::PatternPresetFilePageDirection direction,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location
    ) const;
    SequencerPatternPresetActionResult nextPresetId(
        char* out,
        size_t outSize,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;
    SequencerPatternPresetActionResult createFolder(
        const core::state::sequencer::SequencerPatternPresetLocation& location,
        const char* folderName
    ) const;
    SequencerPatternPresetActionResult renameFolder(
        const core::state::sequencer::SequencerPatternPresetLocation& location,
        const char* folderName,
        const char* newFolderName
    ) const;
    SequencerPatternPresetActionResult deleteFolder(
        const core::state::sequencer::SequencerPatternPresetLocation& location,
        const char* folderName
    ) const;
    SequencerPatternPresetActionResult movePreset(
        const core::state::sequencer::SequencerPatternPresetLocation& source,
        const char* presetId,
        const core::state::sequencer::SequencerPatternPresetLocation&
            destination
    ) const;
    SequencerPatternPresetActionResult moveFolder(
        const core::state::sequencer::SequencerPatternPresetLocation& source,
        const char* folderName,
        const core::state::sequencer::SequencerPatternPresetLocation&
            destination
    ) const;
    core::state::sequencer::SequencerPatternPresetTarget captureTarget() const;
    bool targetMatches(
        const core::state::sequencer::SequencerPatternPresetTarget& target
    ) const;
    [[nodiscard]] bool playbackActive() const;
    uint32_t projectRevision() const;
    SequencerPatternPresetInspectResult inspectPreset(
        const char* presetId,
        const core::state::sequencer::SequencerPatternPresetTarget& target,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;
    SequencerPatternPresetActionResult savePreset(
        const char* presetId,
        const core::state::sequencer::SequencerPatternPresetTarget& target,
        bool allowOverwrite,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;
    SequencerPatternPresetActionResult copyFactoryPreset(
        const char* sourcePresetId,
        const core::state::sequencer::SequencerPatternPresetLocation&
            destination = {}
    ) const;
    SequencerPatternPresetActionResult applyPreset(
        const char* presetId,
        const core::state::sequencer::SequencerPatternPresetTarget& target,
        const core::state::sequencer::SequencerPatternPresetPreviewKey&
            expectedPreview,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;
    SequencerPatternPresetActionResult previewPreset(
        const char* presetId,
        const core::state::sequencer::SequencerPatternPresetTarget& target,
        const core::state::sequencer::SequencerPatternPresetPreviewKey&
            expectedPreview,
        SequencerPatternPresetPreviewSession& session,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;
    SequencerPatternPresetActionResult confirmPresetPreview(
        SequencerPatternPresetPreviewSession& session
    ) const;
    SequencerPatternPresetActionResult cancelPresetPreview(
        SequencerPatternPresetPreviewSession& session
    ) const;
    core::state::sequencer::SequencerTrackActivationStatus activationStatus(
        uint8_t trackIndex,
        uint32_t generation
    ) const;
    SequencerPatternPresetActionResult renamePreset(
        const char* presetId,
        const char* expectedSemanticName,
        const char* newSemanticName,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;
    SequencerPatternPresetActionResult deletePreset(
        const char* presetId,
        const char* expectedSemanticName,
        const core::state::sequencer::SequencerPatternPresetLocation&
            location = {}
    ) const;

private:
    core::state::CoreState* state_ = nullptr;
    core::persistence::ProductFileService* files_ = nullptr;
    core::persistence::ProductDirectoryCatalog* catalog_ = nullptr;
};

}  // namespace core::handler
