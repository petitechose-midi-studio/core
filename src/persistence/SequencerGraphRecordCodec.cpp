#include "persistence/SequencerGraphRecordCodec.hpp"

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "state/sequencer/SequencerGraphCanonicalPolicy.hpp"

namespace core::persistence::sequencer_graph_record_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace graph_policy =
    core::state::sequencer::graph_canonical_policy;

using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;
using oc::note::sequencer::StepSequencerVariationRanges;

}  // namespace

FLASHMEM bool encodeSequence(
    const StepSequencerSequence& sequence,
    uint8_t* out,
    uint16_t size
) {
    if (size != SEQUENCE_RECORD_SIZE ||
        !graph_policy::sequenceIsCanonical(sequence)) {
        return false;
    }
    binary::Writer writer(out, size);
    return writer.writeU8(static_cast<uint8_t>(sequence.kind)) &&
           writer.writeU16(sequence.firstStepNode) &&
           writer.writeU8(sequence.length) &&
           writer.writeI8(sequence.offset) &&
           writer.offset() == size;
}

FLASHMEM bool decodeSequence(
    const uint8_t* data,
    uint16_t size,
    StepSequencerSequence& out
) {
    if (size != SEQUENCE_RECORD_SIZE) return false;

    binary::Reader reader(data, size);
    uint8_t kind = 0;
    StepSequencerSequence decoded{};
    if (!reader.readU8(kind) ||
        !reader.readU16(decoded.firstStepNode) ||
        !reader.readU8(decoded.length) ||
        !reader.readI8(decoded.offset) ||
        reader.offset() != size) {
        return false;
    }
    decoded.kind = static_cast<StepSequencerSequenceKind>(kind);
    if (!graph_policy::sequenceIsCanonical(decoded)) return false;
    out = decoded;
    return true;
}

FLASHMEM bool encodeStepNode(
    const StepSequencerStepNode& node,
    uint8_t* out,
    uint16_t size
) {
    if (size != STEP_NODE_RECORD_SIZE ||
        !graph_policy::stepNodeIsCanonical(node)) {
        return false;
    }

    binary::Writer writer(out, size);
    return writer.writeU16(node.flags) &&
           writer.writeI8(node.noteOffset) &&
           writer.writeI16(node.velocityOffset) &&
           writer.writeI16(node.gateOffset) &&
           writer.writeI8(node.nudgeOffset) &&
           writer.writeI16(node.probabilityOffset) &&
           writer.writeU16(node.childSequenceId) &&
           writer.writeU16(node.cycleSetId) &&
           writer.writeU8(node.localVariation.pitchSemitones) &&
           writer.writeU8(node.localVariation.velocity) &&
           writer.writeU8(node.localVariation.gatePercent) &&
           writer.writeU8(node.localVariation.nudge) &&
           writer.writeU8(static_cast<uint8_t>(node.chordMode)) &&
           writer.writeU8(node.chordSpec.voiceCount) &&
           writer.writeU8(node.chordSpec.harmonyData) &&
           writer.writeU8(node.chordSpec.voicingData) &&
           writer.writeU8(node.chordSpec.inversionData) &&
           writer.writeI8(node.chordSpec.strum) &&
           writer.writeI8(node.chordSpec.velocityCurve) &&
           writer.writeU8(node.chordSpec.customIntervalExtension[0]) &&
           writer.writeU8(node.chordSpec.customIntervalExtension[1]) &&
           writer.writeU8(node.chordSpec.customIntervalExtension[2]) &&
           writer.offset() == size;
}

FLASHMEM bool decodeStepNode(
    const uint8_t* data,
    uint16_t size,
    StepSequencerStepNode& out
) {
    if (size != STEP_NODE_RECORD_SIZE) return false;

    binary::Reader reader(data, size);
    uint16_t flags = 0;
    int8_t noteOffset = 0;
    int16_t velocityOffset = 0;
    int16_t gateOffset = 0;
    int8_t nudgeOffset = 0;
    int16_t probabilityOffset = 0;
    uint16_t childSequenceId = 0;
    uint16_t cycleSetId = 0;
    StepSequencerVariationRanges localVariation{};
    uint8_t chordMode = 0;
    StepSequencerChordSpec chordSpec{};
    if (!reader.readU16(flags) ||
        !reader.readI8(noteOffset) ||
        !reader.readI16(velocityOffset) ||
        !reader.readI16(gateOffset) ||
        !reader.readI8(nudgeOffset) ||
        !reader.readI16(probabilityOffset) ||
        !reader.readU16(childSequenceId) ||
        !reader.readU16(cycleSetId) ||
        !reader.readU8(localVariation.pitchSemitones) ||
        !reader.readU8(localVariation.velocity) ||
        !reader.readU8(localVariation.gatePercent) ||
        !reader.readU8(localVariation.nudge) ||
        !reader.readU8(chordMode) ||
        !reader.readU8(chordSpec.voiceCount) ||
        !reader.readU8(chordSpec.harmonyData) ||
        !reader.readU8(chordSpec.voicingData) ||
        !reader.readU8(chordSpec.inversionData) ||
        !reader.readI8(chordSpec.strum) ||
        !reader.readI8(chordSpec.velocityCurve) ||
        !reader.readU8(chordSpec.customIntervalExtension[0]) ||
        !reader.readU8(chordSpec.customIntervalExtension[1]) ||
        !reader.readU8(chordSpec.customIntervalExtension[2]) ||
        reader.offset() != size) {
        return false;
    }

    const StepSequencerStepNode decoded{
        .flags = flags,
        .velocityOffset = velocityOffset,
        .gateOffset = gateOffset,
        .probabilityOffset = probabilityOffset,
        .childSequenceId = childSequenceId,
        .cycleSetId = cycleSetId,
        .localVariation = localVariation,
        .chordSpec = chordSpec,
        .chordMode = static_cast<StepSequencerChordMode>(chordMode),
        .noteOffset = noteOffset,
        .nudgeOffset = nudgeOffset,
    };
    if (!graph_policy::stepNodeIsCanonical(decoded)) return false;
    out = decoded;
    return true;
}

FLASHMEM bool encodeCycleSet(
    const StepSequencerCycleStateSet& cycleSet,
    uint8_t* out,
    uint16_t size
) {
    if (size != CYCLE_SET_RECORD_SIZE) return false;
    binary::Writer writer(out, size);
    return writer.writeU16(cycleSet.firstStateNode) &&
           writer.writeU8(cycleSet.length) &&
           writer.writeI8(cycleSet.offset) &&
           writer.offset() == size;
}

FLASHMEM bool decodeCycleSet(
    const uint8_t* data,
    uint16_t size,
    StepSequencerCycleStateSet& out
) {
    if (size != CYCLE_SET_RECORD_SIZE) return false;
    binary::Reader reader(data, size);
    StepSequencerCycleStateSet decoded{};
    if (!reader.readU16(decoded.firstStateNode) ||
        !reader.readU8(decoded.length) ||
        !reader.readI8(decoded.offset) ||
        reader.offset() != size) {
        return false;
    }
    out = decoded;
    return true;
}

}  // namespace core::persistence::sequencer_graph_record_codec
