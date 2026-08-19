#pragma once

#include <cstdint>

#include "state/contextual/ContextActionSpec.hpp"
#include "state/sequencer/SequencerPresetMetadata.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

enum class SequencerPatternPresetStatus : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    INVALID_FORMAT,
    UNSUPPORTED_VERSION,
    BUFFER_TOO_SMALL,
    INCOMPATIBLE_TARGET,
    RESOURCE_EXHAUSTED,
};

enum class SequencerPatternPresetCompatibility : uint8_t {
    READY = 0,
    WRONG_TRACK_KIND,
    INCOMPATIBLE_DRUM_KIT,
    STALE_TARGET,
    CORRUPT,
    STORAGE_UNAVAILABLE,
};

enum class SequencerPatternPresetSource : uint8_t {
    USER = 0,
    FACTORY,
};

enum class SequencerPatternPresetSourceFilter : uint8_t {
    ALL = 0,
    FACTORY,
    USER,
};

struct SequencerPatternPresetMetadata {
    static constexpr uint8_t CURRENT_FORMAT_VERSION = 1U;

    uint8_t formatVersion = CURRENT_FORMAT_VERSION;
    SequencerTrackKind trackKind = SequencerTrackKind::INSTRUMENT;
    char technicalId[SEQUENCER_PRESET_TECHNICAL_ID_SIZE] = {};
    char semanticName[SEQUENCER_PRESET_SEMANTIC_NAME_SIZE] = {};
};

struct SequencerPatternPresetTarget {
    bool valid = false;
    uint8_t trackIndex = 0U;
    SequencerTrackKind trackKind = SequencerTrackKind::INSTRUMENT;
    uint32_t projectRevision = 0U;
};

struct SequencerPatternPresetPreviewKey {
    uint32_t assetHash = 0U;
    uint32_t targetHash = 0U;
    uint32_t projectRevision = 0U;

    [[nodiscard]] bool valid() const {
        return assetHash != 0U && targetHash != 0U;
    }
};

constexpr bool operator==(
    const SequencerPatternPresetPreviewKey& lhs,
    const SequencerPatternPresetPreviewKey& rhs
) {
    return lhs.assetHash == rhs.assetHash &&
        lhs.targetHash == rhs.targetHash &&
        lhs.projectRevision == rhs.projectRevision;
}

constexpr bool operator!=(
    const SequencerPatternPresetPreviewKey& lhs,
    const SequencerPatternPresetPreviewKey& rhs
) {
    return !(lhs == rhs);
}

struct SequencerPatternPresetDescriptor {
    bool valid = false;
    SequencerPatternPresetSource source = SequencerPatternPresetSource::USER;
    SequencerPatternPresetMetadata metadata{};
    SequencerPatternPresetCompatibility compatibility =
        SequencerPatternPresetCompatibility::CORRUPT;
    SequencerPatternPresetPreviewKey previewKey{};
    uint8_t patternLength = 0U;
    uint8_t stepsPerBeat = 0U;
    uint8_t drumLaneCount = 0U;
};

bool setSequencerPatternPresetMetadata(
    SequencerPatternPresetMetadata& metadata,
    SequencerTrackKind trackKind,
    const char* technicalId,
    const char* semanticName
);

bool sequencerPatternPresetMetadataIsCanonical(
    const SequencerPatternPresetMetadata& metadata
);

bool sequencerPatternPresetDrumKitCompatible(
    const DrumTrackState& source,
    const DrumTrackState& destination
);

uint32_t sequencerPatternPresetTargetHash(
    const SequencerPatternPresetTarget& target
);

constexpr bool sequencerPatternPresetCanApply(
    SequencerPatternPresetCompatibility compatibility
) {
    return compatibility == SequencerPatternPresetCompatibility::READY;
}

contextual::ContextActionReason sequencerPatternPresetCompatibilityReason(
    SequencerPatternPresetCompatibility compatibility
);

const char* sequencerPatternPresetCompatibilityLabel(
    SequencerPatternPresetCompatibility compatibility
);

const char* sequencerPatternPresetSourceLabel(
    SequencerPatternPresetSource source
);

const char* sequencerPatternPresetSourceFilterLabel(
    SequencerPatternPresetSourceFilter filter
);

contextual::ContextActionSpec buildSequencerPatternPresetActionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset,
    const SequencerPatternPresetTarget& target,
    const SequencerPatternPresetDescriptor& descriptor
);

}  // namespace core::state::sequencer
