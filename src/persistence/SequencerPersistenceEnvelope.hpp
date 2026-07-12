#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "persistence/SequencerPersistencePayloads.hpp"
#include "state/sequencer/SequencerGraphAssetRecords.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::persistence::sequencer_codec {

inline constexpr uint32_t ENVELOPE_HEADER_SIZE = 12;
inline constexpr uint32_t ENVELOPE_SECTION_HEADER_SIZE = 10;
inline constexpr uint32_t MAX_GRAPH_ENVELOPE_SIZE =
    3U * ENVELOPE_SECTION_HEADER_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_SEQUENCES *
        state::sequencer::SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_STEP_NODES *
        state::sequencer::SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE +
    oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_SETS *
        state::sequencer::SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE;
inline constexpr uint32_t MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE =
    ENVELOPE_HEADER_SIZE + ENVELOPE_SECTION_HEADER_SIZE + PATTERN_PAYLOAD_SIZE +
    MAX_GRAPH_ENVELOPE_SIZE;
inline constexpr uint32_t MAX_PROJECT_SEQUENCER_ENVELOPE_PAYLOAD_SIZE =
    ENVELOPE_HEADER_SIZE + ENVELOPE_SECTION_HEADER_SIZE + PROJECT_SEQUENCER_PAYLOAD_SIZE +
    PERSISTED_TRACK_COUNT * MAX_GRAPH_ENVELOPE_SIZE;
inline constexpr uint32_t MAX_SET_ENVELOPE_PAYLOAD_SIZE =
    ENVELOPE_HEADER_SIZE + ENVELOPE_SECTION_HEADER_SIZE + SET_PAYLOAD_SIZE +
    PERSISTED_TRACK_COUNT * MAX_GRAPH_ENVELOPE_SIZE;
inline constexpr uint32_t MAX_ENVELOPE_PAYLOAD_SIZE =
    MAX_PROJECT_SEQUENCER_ENVELOPE_PAYLOAD_SIZE;

static_assert(MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE == 14066U);
static_assert(MAX_SET_ENVELOPE_PAYLOAD_SIZE == 224736U);
static_assert(MAX_PROJECT_SEQUENCER_ENVELOPE_PAYLOAD_SIZE == 224799U);

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
    uint8_t focusedStep = 0;
    state::sequencer::StepProperty activeStepProperty =
        state::sequencer::StepProperty::NOTE;
    std::array<
        const oc::note::sequencer::StepSequencerGraph*,
        PERSISTED_TRACK_COUNT> graphs{};
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
