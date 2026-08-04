#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerChordPreset.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/contextual/ContextActionSpec.hpp"
#include "state/project/ProjectState.hpp"

namespace core::state::sequencer {

enum class SequencerChordPresetCompatibility : uint8_t {
    UNKNOWN = 0,
    READY,
    WARNING_ADAPTED,
    BLOCKED_INCOMPATIBLE,
    CORRUPT,
    STORAGE_UNAVAILABLE,
    STALE_TARGET,
};

struct SequencerChordPresetTarget {
    static constexpr uint16_t INVALID_NODE =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;

    bool valid = false;
    bool canSave = false;
    bool targetUsesScaleDegrees = false;
    uint8_t stepIndex = 0;
    uint16_t nodeId = INVALID_NODE;
    uint8_t rootNote = 60;
    uint8_t velocity = 100;
    uint16_t gate = 100;
    int8_t nudge = 0;
    oc::note::sequencer::StepSequencerScaleSettings scale{};
    oc::note::sequencer::StepSequencerChordSpec captureFormula{};
    oc::note::sequencer::StepSequencerChordHarmony sourceShapeHint =
        oc::note::sequencer::StepSequencerChordHarmony::Custom;
};

struct SequencerChordPresetPreviewKey {
    uint32_t assetFingerprint = 0;
    uint32_t targetHash = 0;
};

constexpr bool operator==(
    const SequencerChordPresetPreviewKey& lhs,
    const SequencerChordPresetPreviewKey& rhs
) {
    return lhs.assetFingerprint == rhs.assetFingerprint &&
           lhs.targetHash == rhs.targetHash;
}

constexpr bool operator!=(
    const SequencerChordPresetPreviewKey& lhs,
    const SequencerChordPresetPreviewKey& rhs
) {
    return !(lhs == rhs);
}

struct SequencerChordPresetDescriptor {
    static constexpr size_t NAME_SIZE =
        oc::note::sequencer::StepSequencerChordPreset::SEMANTIC_NAME_SIZE;

    bool valid = false;
    char semanticName[NAME_SIZE] = {};
    char technicalId[
        oc::note::sequencer::StepSequencerChordPreset::TECHNICAL_ID_SIZE
    ] = {};
    SequencerChordPresetCompatibility compatibility =
        SequencerChordPresetCompatibility::UNKNOWN;
    oc::note::sequencer::StepSequencerChordIntervalBasis sourceBasis =
        oc::note::sequencer::StepSequencerChordIntervalBasis::
            ChromaticSemitones;
    oc::note::sequencer::StepSequencerChordIntervalBasis targetBasis =
        oc::note::sequencer::StepSequencerChordIntervalBasis::
            ChromaticSemitones;
    oc::note::sequencer::StepSequencerChordHarmony sourceShapeHint =
        oc::note::sequencer::StepSequencerChordHarmony::Custom;
    oc::note::sequencer::StepSequencerChordSpec sourceFormula{};
    oc::note::sequencer::StepSequencerChordSpec projectedFormula{};
    oc::note::sequencer::StepSequencerChordProjection projection{};
    oc::note::sequencer::StepSequencerChordResolution resolution{};
    SequencerChordPresetPreviewKey previewKey{};
    uint32_t generation = 0;
};

bool sequencerChordPresetCanApply(
    SequencerChordPresetCompatibility compatibility
);
bool sequencerChordPresetHasWarning(
    SequencerChordPresetCompatibility compatibility
);
const char* sequencerChordPresetCompatibilityLabel(
    SequencerChordPresetCompatibility compatibility
);
contextual::ContextActionReason sequencerChordPresetCompatibilityReason(
    SequencerChordPresetCompatibility compatibility
);

uint32_t sequencerChordPresetTargetHash(
    const SequencerChordPresetTarget& target
);
uint32_t sequencerChordPresetAssetFingerprint(
    const oc::note::sequencer::StepSequencerChordPreset& preset
);

contextual::ContextActionSpec buildSequencerChordPresetActionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset,
    const SequencerChordPresetTarget& target,
    const SequencerChordPresetDescriptor& descriptor
);

}  // namespace core::state::sequencer
