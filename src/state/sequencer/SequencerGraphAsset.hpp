#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/SequencerPresetMetadata.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

enum class SequencerGraphAssetStatus : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    INVALID_FORMAT,
    UNSUPPORTED_VERSION,
    INCOMPATIBLE_TARGET,
    GRAPH_LIMIT_REACHED,
    BUFFER_TOO_SMALL,
    RESOURCE_EXHAUSTED,
};

inline const char* sequencerGraphAssetStatusLabel(
    SequencerGraphAssetStatus status
) {
    switch (status) {
        case SequencerGraphAssetStatus::OK: return "OK";
        case SequencerGraphAssetStatus::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case SequencerGraphAssetStatus::INVALID_FORMAT:
            return "INVALID_FORMAT";
        case SequencerGraphAssetStatus::UNSUPPORTED_VERSION:
            return "UNSUPPORTED_VERSION";
        case SequencerGraphAssetStatus::INCOMPATIBLE_TARGET:
            return "INCOMPATIBLE_TARGET";
        case SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED:
            return "GRAPH_LIMIT_REACHED";
        case SequencerGraphAssetStatus::BUFFER_TOO_SMALL:
            return "BUFFER_TOO_SMALL";
        case SequencerGraphAssetStatus::RESOURCE_EXHAUSTED:
            return "RESOURCE_EXHAUSTED";
        default: return "UNKNOWN";
    }
}

enum SequencerGraphAssetReportFlags : uint16_t {
    SEQUENCER_GRAPH_ASSET_REPORT_NONE = 0,
    SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES = 1U << 0,
    SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD = 1U << 1,
    SEQUENCER_GRAPH_ASSET_REPORT_OVERWRITE = 1U << 2,
};

struct SequencerGraphAssetReport {
    SequencerGraphAssetStatus status = SequencerGraphAssetStatus::OK;
    uint16_t flags = SEQUENCER_GRAPH_ASSET_REPORT_NONE;
    uint16_t stepNodeCount = 0;
    uint8_t sequenceCount = 0;
    uint8_t cycleSetCount = 0;

    void reset();
    bool ok() const { return status == SequencerGraphAssetStatus::OK; }
};

struct SequencerStepGraphPreset {
    static constexpr uint16_t ASSET_ROOT_NODE_ID = 0;
    static constexpr uint8_t CURRENT_FORMAT_VERSION = 5;
    static constexpr size_t TECHNICAL_ID_SIZE =
        SEQUENCER_PRESET_TECHNICAL_ID_SIZE;
    static constexpr size_t SEMANTIC_NAME_SIZE =
        SEQUENCER_PRESET_SEMANTIC_NAME_SIZE;

    enum class ScalePolicy : uint8_t {
        CHROMATIC = 0,
        SCALE_RELATIVE,
    };

    bool valid = false;
    uint8_t formatVersion = CURRENT_FORMAT_VERSION;
    char technicalId[TECHNICAL_ID_SIZE] = {};
    char semanticName[SEMANTIC_NAME_SIZE] = {};
    ScalePolicy scalePolicy = ScalePolicy::CHROMATIC;
    oc::note::sequencer::StepSequencerScaleSettings sourceScale{};
    bool rootContext = true;
    bool rootValuesValid = false;
    bool enabled = false;
    uint8_t note = SequencerState::DEFAULT_NOTE;
    uint8_t velocity = SequencerState::DEFAULT_VELOCITY;
    uint16_t gate = SequencerState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
    oc::note::sequencer::StepSequencerGraph graph{};

    void reset();
};

/**
 * Flat values owned by a root Step outside SequencerPatternState.
 *
 * Drum lanes keep their note identity and per-hit values in DrumPatternState,
 * while their optional Micro/Cycle graph still lives in the shared Pattern
 * graph. This value object lets the one Step-preset format capture that split
 * ownership without staging fake Pattern data or introducing a Drum format.
 */
struct SequencerStepGraphRootValues {
    bool enabled = false;
    uint8_t note = SequencerState::DEFAULT_NOTE;
    uint8_t velocity = SequencerState::DEFAULT_VELOCITY;
    uint16_t gate = SequencerState::DEFAULT_GATE_PERCENT;
    int8_t nudge = 0;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
};

bool captureStepGraphPreset(
    const SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report = nullptr
);

bool captureRootStepGraphPreset(
    const SequencerPatternState& pattern,
    SequencerGraphNodeId sourceNodeId,
    const SequencerStepGraphRootValues& rootValues,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report = nullptr
);

bool applyStepGraphPreset(
    SequencerState& sequencer,
    uint8_t step,
    const SequencerStepGraphPreset& preset,
    SequencerGraphAssetReport* report = nullptr
);

/** Copy only the graph payload of a preset into an explicit shared node. */
bool applyStepGraphPresetGraphToNode(
    SequencerPatternState& pattern,
    SequencerGraphNodeId targetNodeId,
    const SequencerStepGraphPreset& preset,
    SequencerGraphAssetReport* report = nullptr
);

/**
 * Project a shared Step preset onto a destination whose pitch is externally
 * owned (a Drum lane). All pitch/chord semantics are removed recursively;
 * timing, expression, MicroSequence and Cycle content are retained.
 */
bool projectStepGraphPresetToDestinationPitch(
    SequencerStepGraphPreset& preset,
    uint8_t destinationNote,
    bool* changed = nullptr
);

bool validStepGraphPresetTechnicalId(const char* technicalId);
bool validStepGraphPresetSemanticName(const char* semanticName);

bool setStepGraphPresetMetadata(
    SequencerStepGraphPreset& preset,
    const char* technicalId,
    const char* semanticName,
    SequencerStepGraphPreset::ScalePolicy scalePolicy,
    oc::note::sequencer::StepSequencerScaleSettings sourceScale
);

/**
 * Transpose a scale-relative preset from its stored source scale into the
 * destination scale. Chromatic assets are unchanged.
 */
bool adaptStepGraphPresetPitchToDestination(
    SequencerStepGraphPreset& preset,
    oc::note::sequencer::StepSequencerScaleSettings destinationScale,
    bool* changed = nullptr
);

/**
 * Shared domain contracts used by apply and persistence. These functions do
 * not inspect file headers or byte layouts.
 */
bool stepGraphPresetMetadataIsCanonical(
    const SequencerStepGraphPreset& preset
);

bool stepGraphPresetGraphIsCanonical(
    const oc::note::sequencer::StepSequencerGraph& graph
);

}  // namespace core::state::sequencer
