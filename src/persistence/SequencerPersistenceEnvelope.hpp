#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "persistence/DrumTrackPersistenceCodec.hpp"
#include "persistence/SequencerCcLanePersistenceCodec.hpp"
#include "persistence/SequencerGraphRecordCodec.hpp"
#include "persistence/SequencerPersistencePayloads.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence::sequencer_codec {

inline constexpr uint8_t ENVELOPE_VERSION = 15;
inline constexpr uint8_t LEGACY_ENVELOPE_VERSION = 11;
inline constexpr uint32_t ENVELOPE_HEADER_SIZE = 12;
inline constexpr uint32_t ENVELOPE_SECTION_HEADER_SIZE = 10;
inline constexpr uint16_t PATTERN_REGION_RECORD_SIZE = 3;
inline constexpr uint32_t MAX_GRAPH_ENVELOPE_SIZE =
    3U * ENVELOPE_SECTION_HEADER_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_SEQUENCES *
        sequencer_graph_record_codec::SEQUENCE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_STEP_NODES *
        sequencer_graph_record_codec::STEP_NODE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_SETS *
        sequencer_graph_record_codec::CYCLE_SET_RECORD_SIZE;
inline constexpr uint32_t MAX_CC_LANE_ENVELOPE_SIZE =
    ENVELOPE_SECTION_HEADER_SIZE + SEQUENCER_CC_LANE_BANK_RECORD_SIZE;
inline constexpr uint32_t MAX_PATTERN_REGION_ENVELOPE_SIZE =
    ENVELOPE_SECTION_HEADER_SIZE + PATTERN_REGION_RECORD_SIZE;
inline constexpr uint32_t MAX_DRUM_TRACK_ENVELOPE_SIZE =
    ENVELOPE_SECTION_HEADER_SIZE + DRUM_TRACK_RECORD_SIZE +
    MAX_GRAPH_ENVELOPE_SIZE;
inline constexpr uint32_t MAX_TRACK_CONTENT_ENVELOPE_SIZE =
    (MAX_GRAPH_ENVELOPE_SIZE + MAX_CC_LANE_ENVELOPE_SIZE) >
            MAX_DRUM_TRACK_ENVELOPE_SIZE
        ? (MAX_GRAPH_ENVELOPE_SIZE + MAX_CC_LANE_ENVELOPE_SIZE)
        : MAX_DRUM_TRACK_ENVELOPE_SIZE;
inline constexpr uint32_t MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE =
    ENVELOPE_HEADER_SIZE + ENVELOPE_SECTION_HEADER_SIZE + PATTERN_PAYLOAD_SIZE +
    MAX_GRAPH_ENVELOPE_SIZE + MAX_CC_LANE_ENVELOPE_SIZE +
    MAX_PATTERN_REGION_ENVELOPE_SIZE;
inline constexpr uint32_t MAX_PROJECT_SEQUENCER_ENVELOPE_PAYLOAD_SIZE =
    ENVELOPE_HEADER_SIZE + ENVELOPE_SECTION_HEADER_SIZE + PROJECT_SEQUENCER_PAYLOAD_SIZE +
    PERSISTED_TRACK_COUNT *
        (MAX_TRACK_CONTENT_ENVELOPE_SIZE + MAX_PATTERN_REGION_ENVELOPE_SIZE);
inline constexpr uint32_t MAX_SET_ENVELOPE_PAYLOAD_SIZE =
    ENVELOPE_HEADER_SIZE + ENVELOPE_SECTION_HEADER_SIZE + SET_PAYLOAD_SIZE +
    PERSISTED_TRACK_COUNT *
        (MAX_TRACK_CONTENT_ENVELOPE_SIZE + MAX_PATTERN_REGION_ENVELOPE_SIZE);
inline constexpr uint32_t MAX_ENVELOPE_PAYLOAD_SIZE =
    MAX_PROJECT_SEQUENCER_ENVELOPE_PAYLOAD_SIZE;

static_assert(MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE == 16445U);
static_assert(MAX_SET_ENVELOPE_PAYLOAD_SIZE == 426240U);
static_assert(MAX_PROJECT_SEQUENCER_ENVELOPE_PAYLOAD_SIZE == 426303U);

template<uint32_t Capacity>
struct FixedEnvelopeBuffer {
    std::array<uint8_t, Capacity> bytes;
};

using PatternEnvelopeBuffer = FixedEnvelopeBuffer<MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE>;
using EnvelopeBuffer = FixedEnvelopeBuffer<MAX_ENVELOPE_PAYLOAD_SIZE>;

struct EnvelopeEncodeResult {
    bool ok = false;
    uint32_t size = 0;
};

struct ProjectSequencerSnapshotEncodeSource {
    const state::sequencer::SequencerTrackBankSnapshot* flat = nullptr;
    const state::sequencer::DrumTrackBankSnapshot* drums = nullptr;
    uint8_t focusedStep = 0;
    state::sequencer::StepProperty activeStepProperty =
        state::sequencer::StepProperty::NOTE;
    std::array<
        const oc::note::sequencer::StepSequencerGraph*,
        PERSISTED_TRACK_COUNT> graphs{};
    std::array<
        const state::sequencer::SequencerCcLaneBank*,
        PERSISTED_TRACK_COUNT> ccLanes{};
};

EnvelopeEncodeResult fillPatternEnvelope(
    const state::sequencer::SequencerPatternState& source,
    uint8_t* out,
    uint32_t capacity
);

bool applyPatternEnvelope(const uint8_t* data,
                          uint32_t size,
                          state::sequencer::SequencerPatternState& target);

EnvelopeEncodeResult fillProjectSequencerEnvelope(
    const ProjectSequencerSnapshotEncodeSource& source,
    uint8_t* out,
    uint32_t capacity
);

bool applyProjectSequencerEnvelope(const uint8_t* data,
                                   uint32_t size,
                                   state::sequencer::SequencerTrackBankState& trackBank,
                                   state::sequencer::SequencerState& active);

EnvelopeEncodeResult fillSetEnvelope(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& active,
    uint8_t* out,
    uint32_t capacity
);

bool applySetEnvelope(const uint8_t* data,
                      uint32_t size,
                      state::sequencer::SequencerTrackBankState& trackBank,
                      state::sequencer::SequencerState& active);

}  // namespace core::persistence::sequencer_codec
