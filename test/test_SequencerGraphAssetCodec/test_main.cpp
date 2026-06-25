#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphAssetCodec.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"

namespace {

using core::state::sequencer::SequencerGraphAssetReport;
using core::state::sequencer::SequencerGraphAssetStatus;
using core::state::sequencer::SequencerState;
using core::state::sequencer::SequencerStepGraphPreset;
using core::state::sequencer::StepProperty;
using core::state::sequencer::activeContentStepNodeId;
using core::state::sequencer::applyStepGraphPreset;
using core::state::sequencer::captureStepGraphPreset;
using core::state::sequencer::createCycleStateSet;
using core::state::sequencer::createMicroSequence;
using core::state::sequencer::decodeStepGraphPreset;
using core::state::sequencer::encodeStepGraphPreset;
using core::state::sequencer::enterMicroSequenceContentView;
using core::state::sequencer::graphView;
using core::state::sequencer::nodeLocalVariationRange;
using core::state::sequencer::rootStepNodeId;
using core::state::sequencer::setNodeChordMode;
using core::state::sequencer::setNodeChordSpec;
using core::state::sequencer::setNodeEnabledOverride;
using core::state::sequencer::setNodeGateOffset;
using core::state::sequencer::setNodeLocalVariationRange;
using core::state::sequencer::setNodeNoteOffset;
using core::state::sequencer::setNodeNudgeOffset;
using core::state::sequencer::setNodeProbabilityOffset;
using core::state::sequencer::setNodeVelocityOffset;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
using oc::note::sequencer::STEP_NODE_ENABLED_VALUE;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerGraphLimits;

bool reportHas(const SequencerGraphAssetReport& report, uint16_t flag) {
    return (report.flags & flag) != 0;
}

StepSequencerChordSpec makeChord(
    uint8_t voices,
    uint8_t color,
    uint8_t variant,
    uint8_t spread,
    int8_t strum,
    int8_t velocityCurve
) {
    StepSequencerChordSpec spec{};
    spec.voiceCount = voices;
    spec.color = color;
    spec.variant = variant;
    spec.spread = spread;
    spec.strum = strum;
    spec.velocityCurve = velocityCurve;
    return spec;
}

void assertSameChordSpec(
    const StepSequencerChordSpec& actual,
    const StepSequencerChordSpec& expected
) {
    assert(actual.voiceCount == expected.voiceCount);
    assert(actual.color == expected.color);
    assert(actual.variant == expected.variant);
    assert(actual.spread == expected.spread);
    assert(actual.strum == expected.strum);
    assert(actual.velocityCurve == expected.velocityCurve);
}

void test_root_step_graph_preset_roundtrip_preserves_nested_payload() {
    SequencerState source;
    source.pattern.length.set(8);
    source.pattern.setEnabled(2, true);
    assert(source.setStepDataAt(2, 65, 91, 160, -7, 72));

    const auto rootNode = rootStepNodeId(2);
    const auto rootChord = makeChord(6, 3, 2, 5, -18, 9);
    assert(setNodeChordSpec(source.pattern, rootNode, rootChord));
    assert(setNodeVelocityOffset(source.pattern, rootNode, -11));
    assert(setNodeLocalVariationRange(source.pattern, rootNode, StepProperty::NOTE, 5));

    const auto micro = createMicroSequence(source.pattern, rootNode, 3);
    assert(micro.ok);
    const auto* sourceGraph = graphView(source.pattern);
    assert(sourceGraph != nullptr);
    const auto* microSequence = sourceGraph->sequence(micro.id);
    assert(microSequence != nullptr);
    const auto microNode = static_cast<uint16_t>(microSequence->firstStepNode + 1);
    assert(setNodeEnabledOverride(source.pattern, microNode, true));
    assert(setNodeNoteOffset(source.pattern, microNode, 7));
    assert(setNodeVelocityOffset(source.pattern, microNode, -20));
    assert(setNodeGateOffset(source.pattern, microNode, 25));
    assert(setNodeNudgeOffset(source.pattern, microNode, -6));
    assert(setNodeProbabilityOffset(source.pattern, microNode, -30));
    assert(setNodeLocalVariationRange(source.pattern, microNode, StepProperty::VELOCITY, 22));
    assert(setNodeChordMode(source.pattern, microNode, StepSequencerChordMode::Inherit));

    const auto nestedCycle = createCycleStateSet(source.pattern, microNode, 2);
    assert(nestedCycle.ok);
    sourceGraph = graphView(source.pattern);
    assert(sourceGraph != nullptr);
    const auto* nestedCycleSet = sourceGraph->cycleSet(nestedCycle.id);
    assert(nestedCycleSet != nullptr);
    const auto nestedCycleNode = static_cast<uint16_t>(nestedCycleSet->firstStateNode + 1);
    assert(setNodeNoteOffset(source.pattern, nestedCycleNode, 4));
    assert(setNodeLocalVariationRange(source.pattern, nestedCycleNode, StepProperty::GATE, 18));

    SequencerStepGraphPreset captured{};
    SequencerGraphAssetReport report{};
    assert(captureStepGraphPreset(source, 2, captured, &report));
    assert(report.ok());
    assert(reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES));
    assert(reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD));
    assert(report.stepNodeCount >= 6);
    assert(report.sequenceCount == 1);
    assert(report.cycleSetCount == 1);

    std::array<uint8_t, 4096> bytes{};
    const auto encoded = encodeStepGraphPreset(captured, bytes.data(), bytes.size());
    assert(encoded.ok());
    assert(encoded.bytesWritten > 0);

    SequencerStepGraphPreset decoded{};
    SequencerGraphAssetReport decodeReport{};
    assert(decodeStepGraphPreset(bytes.data(), encoded.bytesWritten, decoded, &decodeReport));
    assert(decodeReport.ok());
    assert(decoded.rootContext);
    assert(decoded.rootValuesValid);

    SequencerState target;
    target.pattern.length.set(8);
    target.pattern.setEnabled(5, true);
    assert(target.setStepDataAt(5, 40, 10, 50, 3, 100));
    assert(applyStepGraphPreset(target, 5, decoded, &report));
    assert(report.ok());
    assert(reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_OVERWRITE));

    assert(target.pattern.isEnabled(5));
    assert(target.pattern.note[5] == 65);
    assert(target.pattern.velocity[5] == 91);
    assert(target.pattern.gate[5] == 160);
    assert(target.pattern.nudge[5] == -7);
    assert(target.pattern.probability[5] == 72);

    const auto* targetGraph = graphView(target.pattern);
    assert(targetGraph != nullptr);
    const auto* targetRoot = targetGraph->stepNode(rootStepNodeId(5));
    assert(targetRoot != nullptr);
    assert(targetRoot->has(STEP_NODE_CHORD_MODE));
    assert(targetRoot->has(STEP_NODE_CHORD_LOCAL));
    assertSameChordSpec(targetRoot->chordSpec, rootChord);
    assert(targetRoot->velocityOffset == -11);
    assert(nodeLocalVariationRange(*targetRoot, StepProperty::NOTE) == 5);
    assert(targetRoot->has(STEP_NODE_CHILD_SEQUENCE));
    assert(!targetRoot->has(STEP_NODE_CYCLE_SET));

    const auto* targetMicroSequence = targetGraph->sequence(targetRoot->childSequenceId);
    assert(targetMicroSequence != nullptr);
    assert(targetMicroSequence->length == 3);
    const auto* targetMicroNode = targetGraph->stepNode(
        static_cast<uint16_t>(targetMicroSequence->firstStepNode + 1)
    );
    assert(targetMicroNode != nullptr);
    assert(targetMicroNode->has(STEP_NODE_ENABLED_OVERRIDE));
    assert(targetMicroNode->has(STEP_NODE_ENABLED_VALUE));
    assert(targetMicroNode->noteOffset == 7);
    assert(targetMicroNode->velocityOffset == -20);
    assert(targetMicroNode->gateOffset == 25);
    assert(targetMicroNode->nudgeOffset == -6);
    assert(targetMicroNode->probabilityOffset == -30);
    assert(targetMicroNode->chordMode == StepSequencerChordMode::Inherit);
    assert(nodeLocalVariationRange(*targetMicroNode, StepProperty::VELOCITY) == 22);
    assert(targetMicroNode->has(STEP_NODE_CYCLE_SET));

    const auto* targetCycleSet = targetGraph->cycleSet(targetMicroNode->cycleSetId);
    assert(targetCycleSet != nullptr);
    assert(targetCycleSet->length == 2);
    const auto* targetCycleNode = targetGraph->stepNode(
        static_cast<uint16_t>(targetCycleSet->firstStateNode + 1)
    );
    assert(targetCycleNode != nullptr);
    assert(targetCycleNode->noteOffset == 4);
    assert(nodeLocalVariationRange(*targetCycleNode, StepProperty::GATE) == 18);

    std::cout << "[PASS] test_root_step_graph_preset_roundtrip_preserves_nested_payload\n";
}

void test_child_step_graph_preset_roundtrip_preserves_local_payload_only() {
    SequencerState source;
    source.pattern.length.set(8);
    const auto sourceMicro = createMicroSequence(source.pattern, rootStepNodeId(1), 2);
    assert(sourceMicro.ok);
    assert(enterMicroSequenceContentView(source, rootStepNodeId(1), sourceMicro.id));
    const auto sourceChildNode = activeContentStepNodeId(source, 1);
    assert(sourceChildNode != StepSequencerGraphLimits::INVALID_ID);
    const auto localChord = makeChord(4, 2, 1, 6, 12, -8);
    assert(setNodeChordSpec(source.pattern, sourceChildNode, localChord));
    assert(setNodeNoteOffset(source.pattern, sourceChildNode, -5));
    assert(setNodeNudgeOffset(source.pattern, sourceChildNode, 14));
    assert(setNodeLocalVariationRange(source.pattern, sourceChildNode, StepProperty::NUDGE, 11));

    const auto nestedMicro = createMicroSequence(source.pattern, sourceChildNode, 2);
    assert(nestedMicro.ok);

    SequencerStepGraphPreset captured{};
    SequencerGraphAssetReport report{};
    assert(captureStepGraphPreset(source, 1, captured, &report));
    assert(report.ok());
    assert(!captured.rootContext);
    assert(!captured.rootValuesValid);
    assert(reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD));
    assert(!reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES));

    std::array<uint8_t, 4096> bytes{};
    const auto encoded = encodeStepGraphPreset(captured, bytes.data(), bytes.size());
    assert(encoded.ok());

    SequencerStepGraphPreset decoded{};
    assert(decodeStepGraphPreset(bytes.data(), encoded.bytesWritten, decoded, nullptr));
    assert(!decoded.rootContext);

    SequencerState target;
    target.pattern.length.set(8);
    const auto targetMicro = createMicroSequence(target.pattern, rootStepNodeId(3), 3);
    assert(targetMicro.ok);
    assert(enterMicroSequenceContentView(target, rootStepNodeId(3), targetMicro.id));
    assert(applyStepGraphPreset(target, 2, decoded, &report));
    assert(report.ok());

    const auto targetChildNode = activeContentStepNodeId(target, 2);
    const auto* targetGraph = graphView(target.pattern);
    assert(targetGraph != nullptr);
    const auto* node = targetGraph->stepNode(targetChildNode);
    assert(node != nullptr);
    assert(node->noteOffset == -5);
    assert(node->nudgeOffset == 14);
    assert(node->has(STEP_NODE_CHORD_MODE));
    assert(node->has(STEP_NODE_CHORD_LOCAL));
    assertSameChordSpec(node->chordSpec, localChord);
    assert(nodeLocalVariationRange(*node, StepProperty::NUDGE) == 11);
    assert(node->has(STEP_NODE_CHILD_SEQUENCE));

    std::cout << "[PASS] test_child_step_graph_preset_roundtrip_preserves_local_payload_only\n";
}

void test_context_mismatch_is_reported() {
    SequencerState rootSource;
    rootSource.pattern.length.set(8);
    SequencerStepGraphPreset rootPreset{};
    SequencerGraphAssetReport report{};
    assert(captureStepGraphPreset(rootSource, 0, rootPreset, &report));

    SequencerState childTarget;
    childTarget.pattern.length.set(8);
    const auto micro = createMicroSequence(childTarget.pattern, rootStepNodeId(0), 2);
    assert(micro.ok);
    assert(enterMicroSequenceContentView(childTarget, rootStepNodeId(0), micro.id));
    assert(!applyStepGraphPreset(childTarget, 0, rootPreset, &report));
    assert(report.status == SequencerGraphAssetStatus::INCOMPATIBLE_TARGET);

    std::cout << "[PASS] test_context_mismatch_is_reported\n";
}

void test_decode_rejects_invalid_buffers() {
    SequencerStepGraphPreset decoded{};
    SequencerGraphAssetReport report{};
    const std::array<uint8_t, 4> tiny{0, 1, 2, 3};
    assert(!decodeStepGraphPreset(tiny.data(), tiny.size(), decoded, &report));
    assert(report.status == SequencerGraphAssetStatus::INVALID_ARGUMENT);

    SequencerState source;
    source.pattern.length.set(8);
    SequencerStepGraphPreset preset{};
    assert(captureStepGraphPreset(source, 0, preset, nullptr));
    std::array<uint8_t, 512> bytes{};
    const auto encoded = encodeStepGraphPreset(preset, bytes.data(), bytes.size());
    assert(encoded.ok());
    bytes[0] ^= 0xFFU;
    assert(!decodeStepGraphPreset(bytes.data(), encoded.bytesWritten, decoded, &report));
    assert(report.status == SequencerGraphAssetStatus::INVALID_FORMAT);

    std::cout << "[PASS] test_decode_rejects_invalid_buffers\n";
}

}  // namespace

int main() {
    test_root_step_graph_preset_roundtrip_preserves_nested_payload();
    test_child_step_graph_preset_roundtrip_preserves_local_payload_only();
    test_context_mismatch_is_reported();
    test_decode_rejects_invalid_buffers();
    std::cout << "[PASS] SequencerGraphAssetCodec tests\n";
    return 0;
}
