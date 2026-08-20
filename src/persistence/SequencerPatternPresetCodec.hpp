#pragma once

#include <cstdint>

#include "persistence/DrumTrackPersistenceCodec.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/sequencer/SequencerPatternPreset.hpp"

namespace core::persistence::sequencer_pattern_preset_codec {

struct EncodeResult {
    state::sequencer::SequencerPatternPresetStatus status =
        state::sequencer::SequencerPatternPresetStatus::OK;
    uint16_t bytesWritten = 0U;

    [[nodiscard]] bool ok() const {
        return status == state::sequencer::SequencerPatternPresetStatus::OK;
    }
};

struct MetadataView {
    state::sequencer::SequencerPatternPresetMetadata metadata{};
    uint16_t patternEnvelopeSize = 0U;
    uint16_t drumRecordSize = 0U;
    uint32_t payloadCrc32 = 0U;
};

inline constexpr uint16_t BASE_HEADER_SIZE = 16U;
inline constexpr uint16_t TECHNICAL_ID_OFFSET = BASE_HEADER_SIZE;
inline constexpr uint16_t SEMANTIC_NAME_OFFSET = TECHNICAL_ID_OFFSET +
    state::sequencer::SEQUENCER_PRESET_TECHNICAL_ID_SIZE;
inline constexpr uint16_t METADATA_SIZE =
    state::sequencer::SEQUENCER_PRESET_TECHNICAL_ID_SIZE +
    state::sequencer::SEQUENCER_PRESET_SEMANTIC_NAME_SIZE;
inline constexpr uint16_t HEADER_SIZE = BASE_HEADER_SIZE + METADATA_SIZE;
inline constexpr uint32_t MAX_ENCODED_SIZE_U32 =
    HEADER_SIZE + sequencer_codec::MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE +
    sequencer_codec::DRUM_TRACK_RECORD_SIZE;
static_assert(MAX_ENCODED_SIZE_U32 <= UINT16_MAX);
inline constexpr uint16_t MAX_ENCODED_SIZE =
    static_cast<uint16_t>(MAX_ENCODED_SIZE_U32);

EncodeResult encode(
    const state::sequencer::SequencerPatternPresetMetadata& metadata,
    const state::sequencer::SequencerPatternState& pattern,
    const state::sequencer::DrumTrackState* sourceDrum,
    uint8_t* out,
    uint16_t capacity
);

bool decodeMetadata(
    const uint8_t* data,
    uint16_t size,
    MetadataView& out,
    state::sequencer::SequencerPatternPresetStatus* status = nullptr
);

bool decode(
    const uint8_t* data,
    uint16_t size,
    state::sequencer::SequencerPatternPresetMetadata& metadataOut,
    state::sequencer::SequencerPatternState& patternScratchOut,
    state::sequencer::DrumTrackState* drumScratchOut,
    state::sequencer::SequencerPatternPresetStatus* status = nullptr
);

}  // namespace core::persistence::sequencer_pattern_preset_codec
