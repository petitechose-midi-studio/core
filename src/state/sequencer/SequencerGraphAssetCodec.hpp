#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/SequencerGraphAssetRecords.hpp"
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

inline const char* sequencerGraphAssetStatusLabel(SequencerGraphAssetStatus status) {
    switch (status) {
        case SequencerGraphAssetStatus::OK: return "OK";
        case SequencerGraphAssetStatus::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case SequencerGraphAssetStatus::INVALID_FORMAT: return "INVALID_FORMAT";
        case SequencerGraphAssetStatus::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case SequencerGraphAssetStatus::INCOMPATIBLE_TARGET: return "INCOMPATIBLE_TARGET";
        case SequencerGraphAssetStatus::GRAPH_LIMIT_REACHED: return "GRAPH_LIMIT_REACHED";
        case SequencerGraphAssetStatus::BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
        case SequencerGraphAssetStatus::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
        default: return "UNKNOWN";
    }
}

enum SequencerGraphAssetReportFlags : uint16_t {
    SEQUENCER_GRAPH_ASSET_REPORT_NONE = 0,
    SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES = 1U << 0,
    SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD = 1U << 1,
    SEQUENCER_GRAPH_ASSET_REPORT_OVERWRITE = 1U << 2,
    SEQUENCER_GRAPH_ASSET_REPORT_PITCH_POLICY_MIXED = 1U << 3,
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
    static constexpr uint8_t CURRENT_FORMAT_VERSION = 2;
    static constexpr size_t TECHNICAL_ID_SIZE = 55;
    static constexpr size_t SEMANTIC_NAME_SIZE = 32;

    enum class ScalePolicy : uint8_t {
        CHROMATIC = 0,
        SCALE_RELATIVE,
    };

    bool valid = false;
    uint8_t formatVersion = CURRENT_FORMAT_VERSION;
    bool metadataDefaulted = false;
    bool mixedPitchPolicy = false;
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

struct SequencerGraphAssetEncodeResult {
    SequencerGraphAssetStatus status = SequencerGraphAssetStatus::OK;
    uint16_t bytesWritten = 0;

    bool ok() const { return status == SequencerGraphAssetStatus::OK; }
};

struct SequencerStepGraphPresetMetadataView {
    uint8_t formatVersion = 0;
    bool metadataDefaulted = false;
    bool mixedPitchPolicy = false;
    char technicalId[SequencerStepGraphPreset::TECHNICAL_ID_SIZE] = {};
    char semanticName[SequencerStepGraphPreset::SEMANTIC_NAME_SIZE] = {};
    SequencerStepGraphPreset::ScalePolicy scalePolicy =
        SequencerStepGraphPreset::ScalePolicy::CHROMATIC;
    oc::note::sequencer::StepSequencerScaleSettings sourceScale{};
};

inline constexpr uint32_t STEP_GRAPH_PRESET_V1_HEADER_SIZE = 21;
inline constexpr uint32_t STEP_GRAPH_PRESET_METADATA_SIZE =
    4U + SequencerStepGraphPreset::TECHNICAL_ID_SIZE +
    SequencerStepGraphPreset::SEMANTIC_NAME_SIZE;
inline constexpr uint32_t STEP_GRAPH_PRESET_HEADER_SIZE =
    STEP_GRAPH_PRESET_V1_HEADER_SIZE + STEP_GRAPH_PRESET_METADATA_SIZE;
inline constexpr uint32_t STEP_GRAPH_PRESET_MAX_ENCODED_SIZE_U32 =
    STEP_GRAPH_PRESET_HEADER_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_SEQUENCES *
        SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_STEP_NODES *
        SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_SETS *
        SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE;
static_assert(STEP_GRAPH_PRESET_MAX_ENCODED_SIZE_U32 <= UINT16_MAX);
inline constexpr uint16_t STEP_GRAPH_PRESET_MAX_ENCODED_SIZE =
    static_cast<uint16_t>(STEP_GRAPH_PRESET_MAX_ENCODED_SIZE_U32);

bool captureStepGraphPreset(
    const SequencerState& sequencer,
    uint8_t step,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report = nullptr
);

bool applyStepGraphPreset(
    SequencerState& sequencer,
    uint8_t step,
    const SequencerStepGraphPreset& preset,
    SequencerGraphAssetReport* report = nullptr
);

SequencerGraphAssetEncodeResult encodeStepGraphPreset(
    const SequencerStepGraphPreset& preset,
    uint8_t* out,
    uint16_t capacity
);

bool decodeStepGraphPreset(
    const uint8_t* data,
    uint16_t size,
    SequencerStepGraphPreset& out,
    SequencerGraphAssetReport* report = nullptr
);

/** Parses only the bounded header; graph payload bytes are not required. */
bool decodeStepGraphPresetMetadata(
    const uint8_t* data,
    uint16_t size,
    SequencerStepGraphPresetMetadataView& out,
    SequencerGraphAssetReport* report = nullptr
);

/** Bounded metadata setters used by Core and Manager-facing file operations. */
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
 * Applies only the declared pitch portability contract. CHROMATIC leaves the
 * encoded MIDI note untouched. SCALE_RELATIVE maps its source scale degree to
 * the destination scale without mutating either scale configuration.
 */
bool adaptStepGraphPresetPitchToDestination(
    SequencerStepGraphPreset& preset,
    oc::note::sequencer::StepSequencerScaleSettings destinationScale,
    bool* changed = nullptr
);

}  // namespace core::state::sequencer
