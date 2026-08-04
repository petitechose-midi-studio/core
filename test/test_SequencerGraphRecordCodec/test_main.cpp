#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "persistence/SequencerGraphRecordCodec.hpp"
#include "state/sequencer/SequencerGraphCanonicalPolicy.hpp"

namespace {

namespace codec = core::persistence::sequencer_graph_record_codec;
namespace graph_policy =
    core::state::sequencer::graph_canonical_policy;

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::StepSequencerChordHarmony;
using oc::note::sequencer::StepSequencerChordIntervalBasis;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerChordVoicing;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;

void test_sequence_exact_bytes_and_round_trip() {
    const StepSequencerSequence source{
        .kind = StepSequencerSequenceKind::MicroSequence,
        .firstStepNode = 0x1234U,
        .length = 7U,
        .offset = -3,
    };
    std::array<uint8_t, codec::SEQUENCE_RECORD_SIZE> bytes{};
    assert(codec::encodeSequence(source, bytes.data(), bytes.size()));
    assert((bytes == std::array<uint8_t, codec::SEQUENCE_RECORD_SIZE>{
                         0x01U,
                         0x34U,
                         0x12U,
                         0x07U,
                         0xFDU,
                     }));

    StepSequencerSequence decoded{};
    assert(codec::decodeSequence(bytes.data(), bytes.size(), decoded));
    assert(decoded.kind == source.kind);
    assert(decoded.firstStepNode == source.firstStepNode);
    assert(decoded.length == source.length);
    assert(decoded.offset == source.offset);
}

StepSequencerStepNode detailedNode() {
    auto chord = StepSequencerChordSpec::semantic(
        StepSequencerChordHarmony::Custom,
        8U,
        StepSequencerChordVoicing::Wide,
        3U,
        StepSequencerChordIntervalBasis::ScaleDegrees
    );
    chord.setCustomIntervals({0U, 2U, 4U, 6U, 8U, 10U, 12U, 14U});
    chord.strum = -12;
    chord.velocityCurve = 23;
    chord.clamp();

    return StepSequencerStepNode{
        .flags = static_cast<uint16_t>(
            STEP_NODE_NOTE_OFFSET |
            STEP_NODE_CHILD_SEQUENCE |
            STEP_NODE_CYCLE_SET
        ),
        .velocityOffset = -300,
        .gateOffset = 500,
        .probabilityOffset = -1000,
        .childSequenceId = 0x1234U,
        .cycleSetId = 0xABCDU,
        .localVariation = {
            .pitchSemitones = 36U,
            .velocity = 127U,
            .gatePercent = 100U,
            .nudge = 50U,
        },
        .chordSpec = chord,
        .chordMode = StepSequencerChordMode::Local,
        .noteOffset = -5,
        .nudgeOffset = -2,
    };
}

void test_step_node_exact_field_order_and_round_trip() {
    const auto source = detailedNode();
    std::array<uint8_t, codec::STEP_NODE_RECORD_SIZE> bytes{};
    assert(codec::encodeStepNode(source, bytes.data(), bytes.size()));

    assert(bytes[0] == static_cast<uint8_t>(source.flags & 0xFFU));
    assert(bytes[1] == static_cast<uint8_t>((source.flags >> 8U) & 0xFFU));
    assert(bytes[2] == 0xFBU);
    assert(bytes[3] == 0xD4U && bytes[4] == 0xFEU);
    assert(bytes[5] == 0xF4U && bytes[6] == 0x01U);
    assert(bytes[7] == 0xFEU);
    assert(bytes[8] == 0x18U && bytes[9] == 0xFCU);
    assert(bytes[10] == 0x34U && bytes[11] == 0x12U);
    assert(bytes[12] == 0xCDU && bytes[13] == 0xABU);
    assert(bytes[14] == 36U);
    assert(bytes[15] == 127U);
    assert(bytes[16] == 100U);
    assert(bytes[17] == 50U);
    assert(bytes[18] == static_cast<uint8_t>(StepSequencerChordMode::Local));
    assert(bytes[19] == source.chordSpec.voiceCount);
    assert(bytes[20] == source.chordSpec.harmonyData);
    assert(bytes[21] == source.chordSpec.voicingData);
    assert(bytes[22] == source.chordSpec.inversionData);
    assert(bytes[23] == static_cast<uint8_t>(source.chordSpec.strum));
    assert(bytes[24] == static_cast<uint8_t>(source.chordSpec.velocityCurve));
    assert(bytes[25] == source.chordSpec.customIntervalExtension[0]);
    assert(bytes[26] == source.chordSpec.customIntervalExtension[1]);
    assert(bytes[27] == source.chordSpec.customIntervalExtension[2]);

    StepSequencerStepNode decoded{};
    assert(codec::decodeStepNode(bytes.data(), bytes.size(), decoded));
    assert(decoded.flags == source.flags);
    assert(decoded.noteOffset == source.noteOffset);
    assert(decoded.velocityOffset == source.velocityOffset);
    assert(decoded.gateOffset == source.gateOffset);
    assert(decoded.nudgeOffset == source.nudgeOffset);
    assert(decoded.probabilityOffset == source.probabilityOffset);
    assert(decoded.childSequenceId == source.childSequenceId);
    assert(decoded.cycleSetId == source.cycleSetId);
    assert(decoded.localVariation.pitchSemitones ==
           source.localVariation.pitchSemitones);
    assert(decoded.localVariation.velocity == source.localVariation.velocity);
    assert(decoded.localVariation.gatePercent ==
           source.localVariation.gatePercent);
    assert(decoded.localVariation.nudge == source.localVariation.nudge);
    assert(decoded.chordMode == source.chordMode);
    assert(oc::note::sequencer::chordSpecsEqual(
        decoded.chordSpec,
        source.chordSpec
    ));
}

void test_cycle_set_exact_bytes_and_round_trip() {
    const StepSequencerCycleStateSet source{
        .firstStateNode = 0x1234U,
        .length = 4U,
        .offset = -2,
    };
    std::array<uint8_t, codec::CYCLE_SET_RECORD_SIZE> bytes{};
    assert(codec::encodeCycleSet(source, bytes.data(), bytes.size()));
    assert((bytes == std::array<uint8_t, codec::CYCLE_SET_RECORD_SIZE>{
                         0x34U,
                         0x12U,
                         0x04U,
                         0xFEU,
                     }));

    StepSequencerCycleStateSet decoded{};
    assert(codec::decodeCycleSet(bytes.data(), bytes.size(), decoded));
    assert(decoded.firstStateNode == source.firstStateNode);
    assert(decoded.length == source.length);
    assert(decoded.offset == source.offset);
}

void test_non_canonical_records_are_rejected_without_sanitizing() {
    std::array<uint8_t, codec::SEQUENCE_RECORD_SIZE> sequenceBytes{
        0xFFU,
        0x00U,
        0x00U,
        0x01U,
        0x00U,
    };
    StepSequencerSequence sequence{};
    assert(!codec::decodeSequence(
        sequenceBytes.data(),
        sequenceBytes.size(),
        sequence
    ));
    sequence.kind = static_cast<StepSequencerSequenceKind>(0xFFU);
    assert(!codec::encodeSequence(
        sequence,
        sequenceBytes.data(),
        sequenceBytes.size()
    ));

    auto node = detailedNode();
    std::array<uint8_t, codec::STEP_NODE_RECORD_SIZE> nodeBytes{};
    node.flags = static_cast<uint16_t>(node.flags | 0x8000U);
    assert(!codec::encodeStepNode(node, nodeBytes.data(), nodeBytes.size()));

    node = detailedNode();
    node.chordSpec.harmonyData =
        static_cast<uint8_t>(node.chordSpec.harmonyData | 0x80U);
    assert(!codec::encodeStepNode(node, nodeBytes.data(), nodeBytes.size()));

    node = detailedNode();
    node.localVariation.pitchSemitones = 37U;
    assert(!codec::encodeStepNode(node, nodeBytes.data(), nodeBytes.size()));

    node = detailedNode();
    assert(codec::encodeStepNode(node, nodeBytes.data(), nodeBytes.size()));
    nodeBytes[1] = static_cast<uint8_t>(nodeBytes[1] | 0x80U);
    StepSequencerStepNode decoded{};
    assert(!codec::decodeStepNode(
        nodeBytes.data(),
        nodeBytes.size(),
        decoded
    ));

    assert(!codec::encodeStepNode(
        detailedNode(),
        nodeBytes.data(),
        static_cast<uint16_t>(nodeBytes.size() - 1U)
    ));
    assert(!codec::decodeStepNode(nullptr, nodeBytes.size(), decoded));
}

void test_domain_canonical_policy_is_the_shared_record_admission_rule() {
    StepSequencerSequence sequence{
        .kind = StepSequencerSequenceKind::MicroSequence,
    };
    assert(graph_policy::sequenceIsCanonical(sequence));
    sequence.kind = static_cast<StepSequencerSequenceKind>(0xFFU);
    assert(!graph_policy::sequenceIsCanonical(sequence));

    auto node = detailedNode();
    assert(graph_policy::stepNodeIsCanonical(node));
    node.flags = static_cast<uint16_t>(node.flags | 0x8000U);
    assert(!graph_policy::stepNodeIsCanonical(node));
}

}  // namespace

int main() {
    test_sequence_exact_bytes_and_round_trip();
    test_step_node_exact_field_order_and_round_trip();
    test_cycle_set_exact_bytes_and_round_trip();
    test_non_canonical_records_are_rejected_without_sanitizing();
    test_domain_canonical_policy_is_the_shared_record_admission_rule();
    std::cout << "All Sequencer graph record codec tests passed\n";
    return 0;
}
