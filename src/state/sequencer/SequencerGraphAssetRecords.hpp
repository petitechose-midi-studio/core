#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerChord.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

namespace core::state::sequencer {

inline constexpr uint16_t SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE = 5;
inline constexpr uint16_t SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE = 25;
inline constexpr uint16_t SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE = 4;

struct SequencerGraphSequenceRecord {
    uint8_t kind = 0;
    uint16_t firstStepNode = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint8_t length = 0;
    int8_t offset = 0;
};

struct SequencerGraphStepNodeRecord {
    uint16_t flags = 0;
    int8_t noteOffset = 0;
    int16_t velocityOffset = 0;
    int16_t gateOffset = 0;
    int8_t nudgeOffset = 0;
    int16_t probabilityOffset = 0;
    uint16_t childSequenceId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint16_t cycleSetId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint8_t localVariationPitchSemitones = 0;
    uint8_t localVariationVelocity = 0;
    uint8_t localVariationGatePercent = 0;
    uint8_t localVariationNudge = 0;
    uint8_t chordMode = static_cast<uint8_t>(
        oc::note::sequencer::StepSequencerChordMode::Single
    );
    uint8_t chordVoiceCount = 3;
    // Same three persistent bytes for every supported version. Envelopes <= 6
    // and presets <= 2 contain the legacy Color/Variant/Spread recipe. Newer
    // formats use StepSequencerChordSpec's explicit semantic marker and values.
    uint8_t chordHarmonyData = 0;
    uint8_t chordVoicingData = 0;
    uint8_t chordInversionData = 0;
    int8_t chordStrum = 0;
    int8_t chordVelocityCurve = 0;
};

struct SequencerGraphCycleSetRecord {
    uint16_t firstStateNode = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint8_t length = 0;
    int8_t offset = 0;
};

namespace graph_record_codec {

inline bool writeU8(uint8_t* data, uint16_t size, uint16_t& offset, uint8_t value) {
    if (data == nullptr || offset >= size) return false;
    data[offset++] = value;
    return true;
}

inline bool writeI8(uint8_t* data, uint16_t size, uint16_t& offset, int8_t value) {
    return writeU8(data, size, offset, static_cast<uint8_t>(value));
}

inline bool writeU16(uint8_t* data, uint16_t size, uint16_t& offset, uint16_t value) {
    return writeU8(data, size, offset, static_cast<uint8_t>(value & 0xFFU)) &&
           writeU8(data, size, offset, static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

inline bool writeI16(uint8_t* data, uint16_t size, uint16_t& offset, int16_t value) {
    return writeU16(data, size, offset, static_cast<uint16_t>(value));
}

inline bool readU8(const uint8_t* data, uint16_t size, uint16_t& offset, uint8_t& out) {
    if (data == nullptr || offset >= size) return false;
    out = data[offset++];
    return true;
}

inline bool readI8(const uint8_t* data, uint16_t size, uint16_t& offset, int8_t& out) {
    uint8_t raw = 0;
    if (!readU8(data, size, offset, raw)) return false;
    out = static_cast<int8_t>(raw);
    return true;
}

inline bool readU16(const uint8_t* data, uint16_t size, uint16_t& offset, uint16_t& out) {
    uint8_t lo = 0;
    uint8_t hi = 0;
    if (!readU8(data, size, offset, lo) || !readU8(data, size, offset, hi)) return false;
    out = static_cast<uint16_t>(lo | static_cast<uint16_t>(hi << 8U));
    return true;
}

inline bool readI16(const uint8_t* data, uint16_t size, uint16_t& offset, int16_t& out) {
    uint16_t raw = 0;
    if (!readU16(data, size, offset, raw)) return false;
    out = static_cast<int16_t>(raw);
    return true;
}

}  // namespace graph_record_codec

inline bool encodeSequencerGraphSequenceRecord(const SequencerGraphSequenceRecord& record,
                                               uint8_t* out,
                                               uint16_t size) {
    if (size != SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE) return false;
    uint16_t offset = 0;
    return graph_record_codec::writeU8(out, size, offset, record.kind) &&
           graph_record_codec::writeU16(out, size, offset, record.firstStepNode) &&
           graph_record_codec::writeU8(out, size, offset, record.length) &&
           graph_record_codec::writeI8(out, size, offset, record.offset) &&
           offset == size;
}

inline bool decodeSequencerGraphSequenceRecord(const uint8_t* data,
                                               uint16_t size,
                                               SequencerGraphSequenceRecord& record) {
    if (size != SEQUENCER_GRAPH_SEQUENCE_RECORD_SIZE) return false;
    uint16_t offset = 0;
    return graph_record_codec::readU8(data, size, offset, record.kind) &&
           graph_record_codec::readU16(data, size, offset, record.firstStepNode) &&
           graph_record_codec::readU8(data, size, offset, record.length) &&
           graph_record_codec::readI8(data, size, offset, record.offset) &&
           offset == size;
}

inline bool encodeSequencerGraphStepNodeRecord(const SequencerGraphStepNodeRecord& record,
                                               uint8_t* out,
                                               uint16_t size) {
    if (size != SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE) return false;
    uint16_t offset = 0;
    return graph_record_codec::writeU16(out, size, offset, record.flags) &&
           graph_record_codec::writeI8(out, size, offset, record.noteOffset) &&
           graph_record_codec::writeI16(out, size, offset, record.velocityOffset) &&
           graph_record_codec::writeI16(out, size, offset, record.gateOffset) &&
           graph_record_codec::writeI8(out, size, offset, record.nudgeOffset) &&
           graph_record_codec::writeI16(out, size, offset, record.probabilityOffset) &&
           graph_record_codec::writeU16(out, size, offset, record.childSequenceId) &&
           graph_record_codec::writeU16(out, size, offset, record.cycleSetId) &&
           graph_record_codec::writeU8(out, size, offset, record.localVariationPitchSemitones) &&
           graph_record_codec::writeU8(out, size, offset, record.localVariationVelocity) &&
           graph_record_codec::writeU8(out, size, offset, record.localVariationGatePercent) &&
           graph_record_codec::writeU8(out, size, offset, record.localVariationNudge) &&
           graph_record_codec::writeU8(out, size, offset, record.chordMode) &&
           graph_record_codec::writeU8(out, size, offset, record.chordVoiceCount) &&
           graph_record_codec::writeU8(out, size, offset, record.chordHarmonyData) &&
           graph_record_codec::writeU8(out, size, offset, record.chordVoicingData) &&
           graph_record_codec::writeU8(out, size, offset, record.chordInversionData) &&
           graph_record_codec::writeI8(out, size, offset, record.chordStrum) &&
           graph_record_codec::writeI8(out, size, offset, record.chordVelocityCurve) &&
           offset == size;
}

inline bool decodeSequencerGraphStepNodeRecord(const uint8_t* data,
                                               uint16_t size,
                                               SequencerGraphStepNodeRecord& record) {
    if (size != SEQUENCER_GRAPH_STEP_NODE_RECORD_SIZE) return false;
    uint16_t offset = 0;
    return graph_record_codec::readU16(data, size, offset, record.flags) &&
           graph_record_codec::readI8(data, size, offset, record.noteOffset) &&
           graph_record_codec::readI16(data, size, offset, record.velocityOffset) &&
           graph_record_codec::readI16(data, size, offset, record.gateOffset) &&
           graph_record_codec::readI8(data, size, offset, record.nudgeOffset) &&
           graph_record_codec::readI16(data, size, offset, record.probabilityOffset) &&
           graph_record_codec::readU16(data, size, offset, record.childSequenceId) &&
           graph_record_codec::readU16(data, size, offset, record.cycleSetId) &&
           graph_record_codec::readU8(data, size, offset, record.localVariationPitchSemitones) &&
           graph_record_codec::readU8(data, size, offset, record.localVariationVelocity) &&
           graph_record_codec::readU8(data, size, offset, record.localVariationGatePercent) &&
           graph_record_codec::readU8(data, size, offset, record.localVariationNudge) &&
           graph_record_codec::readU8(data, size, offset, record.chordMode) &&
           graph_record_codec::readU8(data, size, offset, record.chordVoiceCount) &&
           graph_record_codec::readU8(data, size, offset, record.chordHarmonyData) &&
           graph_record_codec::readU8(data, size, offset, record.chordVoicingData) &&
           graph_record_codec::readU8(data, size, offset, record.chordInversionData) &&
           graph_record_codec::readI8(data, size, offset, record.chordStrum) &&
           graph_record_codec::readI8(data, size, offset, record.chordVelocityCurve) &&
           offset == size;
}

inline bool encodeSequencerGraphCycleSetRecord(const SequencerGraphCycleSetRecord& record,
                                               uint8_t* out,
                                               uint16_t size) {
    if (size != SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE) return false;
    uint16_t offset = 0;
    return graph_record_codec::writeU16(out, size, offset, record.firstStateNode) &&
           graph_record_codec::writeU8(out, size, offset, record.length) &&
           graph_record_codec::writeI8(out, size, offset, record.offset) &&
           offset == size;
}

inline bool decodeSequencerGraphCycleSetRecord(const uint8_t* data,
                                               uint16_t size,
                                               SequencerGraphCycleSetRecord& record) {
    if (size != SEQUENCER_GRAPH_CYCLE_SET_RECORD_SIZE) return false;
    uint16_t offset = 0;
    return graph_record_codec::readU16(data, size, offset, record.firstStateNode) &&
           graph_record_codec::readU8(data, size, offset, record.length) &&
           graph_record_codec::readI8(data, size, offset, record.offset) &&
           offset == size;
}

}  // namespace core::state::sequencer
