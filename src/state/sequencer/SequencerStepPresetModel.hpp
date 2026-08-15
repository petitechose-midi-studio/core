#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/contextual/ContextActionSpec.hpp"
#include "state/project/ProjectState.hpp"

namespace core::state::sequencer {

enum class SequencerStepPresetScope : uint8_t {
    ROOT = 0,
    CHILD,
};

enum class SequencerStepPresetTargetContext : uint8_t {
    ROOT = 0,
    MICRO_SEQUENCE,
    CYCLE_STATES,
};

enum SequencerStepPresetContentFlags : uint16_t {
    STEP_PRESET_CONTENT_NONE = 0,
    STEP_PRESET_CONTENT_STEP_VALUES = 1U << 0,
    STEP_PRESET_CONTENT_GRAPH = 1U << 1,
    STEP_PRESET_CONTENT_MICRO_SEQUENCE = 1U << 2,
    STEP_PRESET_CONTENT_CYCLE = 1U << 3,
    STEP_PRESET_CONTENT_CHORD = 1U << 4,
    STEP_PRESET_CONTENT_RANDOM = 1U << 5,
};

enum class SequencerStepPresetScalePolicy : uint8_t {
    CHROMATIC = 0,
    SCALE_RELATIVE,
};

enum class SequencerStepPresetAdaptation : uint8_t {
    PRESERVED = 0,
    DESTINATION_SCALE,
    DESTINATION_PITCH,
};

enum class SequencerStepPresetFootprint : uint8_t {
    FREE = 0,
    REPLACE,
};

/**
 * Compatibility taxonomy shared by controller inspection and the file-tool
 * boundary. Warning classes remain executable; every blocked class is visible
 * and carries an actionable reason.
 */
enum class SequencerStepPresetCompatibility : uint8_t {
    UNKNOWN = 0,
    READY,
    WARNING_ADAPTED,
    BLOCKED_CONTEXT,
    BLOCKED_PITCH_CONTEXT,
    BLOCKED_CAPACITY,
    CORRUPT,
    UNSUPPORTED_VERSION,
    STORAGE_UNAVAILABLE,
    STALE_TARGET,
};

struct SequencerStepPresetTarget {
    using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;

    bool valid = false;
    uint8_t trackIndex = 0;
    uint8_t stepIndex = 0;
    SequencerStepPresetTargetContext contentContext =
        SequencerStepPresetTargetContext::ROOT;
    uint16_t ownerNodeId = GraphLimits::INVALID_ID;
    uint16_t sequenceId = GraphLimits::INVALID_ID;
    uint16_t cycleSetId = GraphLimits::INVALID_ID;
    uint16_t targetNodeId = GraphLimits::INVALID_ID;
    // A Drum lane owns pitch independently from its Step payload. These
    // compact fields freeze that identity across asynchronous inspection and
    // apply without creating a second preset model.
    bool destinationOwnsPitch = false;
    uint8_t destinationNote = 0;
    uint8_t drumLaneIndex = 0;
    uint8_t drumRootStepIndex = 0;
    uint8_t drumRootSlot = 0xFFU;
    uint32_t projectRevision = 0;
    char contextLabel[32] = {};
};

struct SequencerStepPresetPreviewKey {
    uint32_t assetHash = 0;
    uint32_t targetHash = 0;
    uint32_t projectRevision = 0;
    uint8_t stateIndex = 0;
};

constexpr bool operator==(
    const SequencerStepPresetPreviewKey& lhs,
    const SequencerStepPresetPreviewKey& rhs
) {
    return lhs.assetHash == rhs.assetHash &&
           lhs.targetHash == rhs.targetHash &&
           lhs.projectRevision == rhs.projectRevision &&
           lhs.stateIndex == rhs.stateIndex;
}

constexpr bool operator!=(
    const SequencerStepPresetPreviewKey& lhs,
    const SequencerStepPresetPreviewKey& rhs
) {
    return !(lhs == rhs);
}

struct SequencerStepPresetDescriptor {
    static constexpr size_t NAME_SIZE = 32;
    static constexpr size_t SUMMARY_SIZE = 40;
    static constexpr size_t FACTS_SIZE = 48;

    bool valid = false;
    char semanticName[NAME_SIZE] = {};
    char technicalId[project::ProjectMetadata::ID_SIZE] = {};
    SequencerStepPresetScope scope = SequencerStepPresetScope::ROOT;
    uint16_t contentFlags = STEP_PRESET_CONTENT_NONE;
    uint16_t stepNodeCount = 0;
    uint8_t sequenceCount = 0;
    uint8_t cycleSetCount = 0;
    SequencerStepPresetScalePolicy scalePolicy =
        SequencerStepPresetScalePolicy::CHROMATIC;
    SequencerStepPresetAdaptation adaptation =
        SequencerStepPresetAdaptation::PRESERVED;
    SequencerStepPresetFootprint footprint =
        SequencerStepPresetFootprint::FREE;
    SequencerStepPresetCompatibility compatibility =
        SequencerStepPresetCompatibility::UNKNOWN;
    uint8_t previewStateIndex = 0;
    uint8_t previewStateCount = 1;
    int16_t previewNote = 0;
    char contentSummary[SUMMARY_SIZE] = {};
    char adaptationSummary[SUMMARY_SIZE] = {};
    char replaceFacts[FACTS_SIZE] = {};
    char preserveFacts[FACTS_SIZE] = {};
    char compatibilityReason[FACTS_SIZE] = {};
    char previewSummary[SUMMARY_SIZE] = {};
    SequencerStepPresetPreviewKey previewKey{};
    uint32_t generation = 0;
};

/**
 * Accepts an inspection payload only for the exact request generation and
 * immutable target/state request that produced it. The resulting asset hash
 * is intentionally not known until the file is read; it is retained in the
 * accepted descriptor for the later apply-time stale check.
 */
bool sequencerStepPresetInspectionMatches(
    uint32_t expectedGeneration,
    const SequencerStepPresetPreviewKey& expectedPreview,
    const SequencerStepPresetDescriptor& descriptor
);

bool sequencerStepPresetCanApply(SequencerStepPresetCompatibility compatibility);
bool sequencerStepPresetHasWarning(SequencerStepPresetCompatibility compatibility);
const char* sequencerStepPresetCompatibilityLabel(
    SequencerStepPresetCompatibility compatibility
);
contextual::ContextActionReason sequencerStepPresetCompatibilityReason(
    SequencerStepPresetCompatibility compatibility
);

uint32_t sequencerStepPresetIdHash(const char* presetId);
uint32_t sequencerStepPresetTargetHash(const SequencerStepPresetTarget& target);
void sequencerStepPresetSemanticName(
    const char* presetId,
    char* out,
    size_t outSize
);

contextual::ContextActionSpec buildSequencerStepPresetActionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset,
    const SequencerStepPresetTarget& target,
    const SequencerStepPresetDescriptor& descriptor
);

}  // namespace core::state::sequencer
