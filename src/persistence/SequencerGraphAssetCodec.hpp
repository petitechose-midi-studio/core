#pragma once

#include <cstdint>

#include "persistence/SequencerGraphRecordCodec.hpp"
#include "state/sequencer/SequencerGraphAsset.hpp"

namespace core::persistence::sequencer_graph_asset_codec {

struct EncodeResult {
    state::sequencer::SequencerGraphAssetStatus status =
        state::sequencer::SequencerGraphAssetStatus::OK;
    uint16_t bytesWritten = 0;

    bool ok() const {
        return status == state::sequencer::SequencerGraphAssetStatus::OK;
    }
};

struct MetadataView {
    uint8_t formatVersion = 0;
    char technicalId[
        state::sequencer::SequencerStepGraphPreset::TECHNICAL_ID_SIZE
    ] = {};
    char semanticName[
        state::sequencer::SequencerStepGraphPreset::SEMANTIC_NAME_SIZE
    ] = {};
    state::sequencer::SequencerStepGraphPreset::ScalePolicy scalePolicy =
        state::sequencer::SequencerStepGraphPreset::ScalePolicy::CHROMATIC;
    oc::note::sequencer::StepSequencerScaleSettings sourceScale{};
};

inline constexpr uint32_t BASE_HEADER_SIZE = 21;
inline constexpr uint32_t METADATA_SIZE =
    4U +
    state::sequencer::SequencerStepGraphPreset::TECHNICAL_ID_SIZE +
    state::sequencer::SequencerStepGraphPreset::SEMANTIC_NAME_SIZE;
inline constexpr uint32_t HEADER_SIZE = BASE_HEADER_SIZE + METADATA_SIZE;
inline constexpr uint32_t MAX_ENCODED_SIZE_U32 =
    HEADER_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_SEQUENCES *
        sequencer_graph_record_codec::SEQUENCE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_STEP_NODES *
        sequencer_graph_record_codec::STEP_NODE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_SETS *
        sequencer_graph_record_codec::CYCLE_SET_RECORD_SIZE;
static_assert(MAX_ENCODED_SIZE_U32 <= UINT16_MAX);
inline constexpr uint16_t MAX_ENCODED_SIZE =
    static_cast<uint16_t>(MAX_ENCODED_SIZE_U32);

EncodeResult encode(
    const state::sequencer::SequencerStepGraphPreset& preset,
    uint8_t* out,
    uint16_t capacity
);

bool decode(
    const uint8_t* data,
    uint16_t size,
    state::sequencer::SequencerStepGraphPreset& out,
    state::sequencer::SequencerGraphAssetReport* report = nullptr
);

bool decodeMetadata(
    const uint8_t* data,
    uint16_t size,
    MetadataView& out,
    state::sequencer::SequencerGraphAssetReport* report = nullptr
);

}  // namespace core::persistence::sequencer_graph_asset_codec
