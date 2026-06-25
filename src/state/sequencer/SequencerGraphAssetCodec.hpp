#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

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
};

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

    bool valid = false;
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

}  // namespace core::state::sequencer
