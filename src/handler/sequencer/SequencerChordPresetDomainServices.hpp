#pragma once

#include <cstddef>
#include <cstdint>

#include "persistence/ChordPresetFileStore.hpp"
#include "state/sequencer/SequencerChordPresetModel.hpp"

namespace core::persistence {
class ProductFileService;
}

namespace core::state {
struct CoreState;
}

namespace core::handler {

enum class SequencerChordPresetStatus : uint8_t {
    OK = 0,
    STORAGE_UNAVAILABLE,
    EMPTY,
    INCOMPATIBLE,
    CORRUPT,
    STALE_TARGET,
    COLLISION,
    FAILED,
};

struct SequencerChordPresetListResult {
    SequencerChordPresetStatus status = SequencerChordPresetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint8_t count = 0;
    bool truncated = false;
    bool hasPrevious = false;
    bool hasNext = false;
    uint16_t totalCount = 0;

    [[nodiscard]] bool ok() const {
        return status == SequencerChordPresetStatus::OK;
    }
};

struct SequencerChordPresetInspectResult {
    SequencerChordPresetStatus status = SequencerChordPresetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    core::state::sequencer::SequencerChordPresetDescriptor descriptor{};
};

struct SequencerChordPresetActionResult {
    SequencerChordPresetStatus status = SequencerChordPresetStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint16_t bytes = 0;
    bool changed = false;
    char presetId[core::state::project::ProjectMetadata::ID_SIZE] = {};

    [[nodiscard]] bool ok() const {
        return status == SequencerChordPresetStatus::OK &&
               fileError == oc::type::ErrorCode::OK;
    }
};

class SequencerChordPresetDomainServices {
public:
    using Entry = core::persistence::ChordPresetFileListEntry;

    SequencerChordPresetDomainServices() = default;
    SequencerChordPresetDomainServices(
        core::state::CoreState& state,
        core::persistence::ProductFileService& files
    );

    static SequencerChordPresetDomainServices fromCoreState(
        core::state::CoreState& state,
        core::persistence::ProductFileService& files
    );

    SequencerChordPresetListResult listPresetsPage(
        Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        core::persistence::ChordPresetFilePageDirection direction
    ) const;
    SequencerChordPresetActionResult nextPresetId(
        char* out,
        size_t outSize
    ) const;

    core::state::sequencer::SequencerChordPresetTarget captureTarget() const;
    bool targetMatches(
        const core::state::sequencer::SequencerChordPresetTarget& target
    ) const;

    SequencerChordPresetInspectResult inspectPreset(
        const char* presetId,
        const core::state::sequencer::SequencerChordPresetTarget& target,
        uint32_t generation
    ) const;

    SequencerChordPresetActionResult savePreset(
        const char* presetId,
        const core::state::sequencer::SequencerChordPresetTarget& target,
        bool allowOverwrite
    ) const;

    SequencerChordPresetActionResult applyPreset(
        const char* presetId,
        const core::state::sequencer::SequencerChordPresetTarget& target,
        const core::state::sequencer::SequencerChordPresetPreviewKey&
            expectedPreview
    ) const;

private:
    core::state::CoreState* state_ = nullptr;
    core::persistence::ProductFileService* files_ = nullptr;
};

}  // namespace core::handler
