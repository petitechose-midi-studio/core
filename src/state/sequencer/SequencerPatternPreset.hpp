#pragma once

#include <array>
#include <cstddef>
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

/**
 * User-library location, deliberately separate from the preset technical id.
 *
 * The path is relative to `library/pattern-presets`, contains only validated
 * folder segments and is bounded for the controller filesystem contract.
 */
struct SequencerPatternPresetLocation {
    static constexpr uint8_t MAX_DEPTH = 4U;
    static constexpr size_t MAX_FOLDER_NAME_SIZE = 32U;
    static constexpr size_t PATH_SIZE = 128U;

    std::array<char, PATH_SIZE> relativeDirectory{};
    uint8_t depth = 0U;

    [[nodiscard]] bool root() const {
        return depth == 0U || relativeDirectory[0] == '\0';
    }
    void reset();
    bool enter(const char* folderName);
    bool leave();
};

bool sequencerPatternPresetFolderNameIsValid(const char* folderName);
bool formatSequencerPatternPresetDirectory(
    const SequencerPatternPresetLocation& location,
    char* out,
    size_t outSize
);

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

/**
 * Bounded, presentation-only thumbnail extracted during preset inspection.
 *
 * The full decoded Pattern is already available on the cold inspect path. A
 * compact projection lets the controller show musical content after that
 * temporary object is released, without retaining another graph or touching
 * live authored state.
 */
struct SequencerPatternPresetVisualSummary {
    static constexpr uint8_t STEP_CAPACITY = 16U;
    static constexpr uint8_t LANE_CAPACITY = 8U;

    bool valid = false;
    uint8_t visibleStepCount = 0U;
    uint8_t laneCount = 0U;
    uint16_t melodicEnabledMask = 0U;
    std::array<uint8_t, STEP_CAPACITY> notes{};
    std::array<uint8_t, STEP_CAPACITY> velocities{};
    std::array<uint16_t, LANE_CAPACITY> drumEnabledMasks{};
    std::array<DrumLaneIcon, LANE_CAPACITY> drumIcons{};
    std::array<uint8_t, LANE_CAPACITY> drumColorIndices{};
    std::array<
        std::array<char, DRUM_LANE_NAME_MAX_LENGTH + 1U>,
        LANE_CAPACITY> drumNames{};
    uint8_t microSequenceCount = 0U;
    uint8_t cycleStateCount = 0U;
    uint8_t ccLaneCount = 0U;
};

static_assert(
    sizeof(SequencerPatternPresetVisualSummary) <= 160U,
    "Pattern preset thumbnail must remain a compact cold projection"
);

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
    SequencerPatternPresetVisualSummary visual{};
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
