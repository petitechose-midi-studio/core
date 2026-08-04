#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../../src/persistence/SequencerGraphAssetCodec.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphAsset.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"

namespace {

namespace asset_codec =
    core::persistence::sequencer_graph_asset_codec;

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
using core::state::sequencer::setStepGraphPresetMetadata;
using core::state::sequencer::SequencerPitchEditMode;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
using oc::note::sequencer::STEP_NODE_ENABLED_VALUE;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordHarmony;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerChordVoicing;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;

bool reportHas(const SequencerGraphAssetReport& report, uint16_t flag) {
    return (report.flags & flag) != 0;
}

StepSequencerScaleSettings constrainedScale() {
    return {
        .root = 5,
        .type = StepSequencerScaleType::HarmonicMinor,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
}

StepSequencerChordSpec makeChord(
    uint8_t voices,
    StepSequencerChordHarmony harmony,
    StepSequencerChordVoicing voicing,
    uint8_t inversion,
    int8_t strum,
    int8_t velocityCurve
) {
    auto spec = StepSequencerChordSpec::semantic(
        harmony,
        voices,
        voicing,
        inversion
    );
    spec.strum = strum;
    spec.velocityCurve = velocityCurve;
    return spec;
}

void assertSameChordSpec(
    const StepSequencerChordSpec& actual,
    const StepSequencerChordSpec& expected
) {
    assert(oc::note::sequencer::chordSpecsEqual(actual, expected));
}

void test_root_step_graph_preset_roundtrip_preserves_nested_payload() {
    SequencerState source;
    source.pattern.setContentLength(8);
    source.pattern.setEnabled(2, true);
    assert(source.setStepDataAt(2, 65, 91, 160, -7, 72));

    const auto rootNode = rootStepNodeId(2);
    const auto rootChord = makeChord(
        6,
        StepSequencerChordHarmony::Quartal,
        StepSequencerChordVoicing::Wide,
        5,
        -18,
        9
    );
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
    assert(captureStepGraphPreset(source, 2, {}, captured, &report));
    assert(report.ok());
    assert(reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES));
    assert(reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD));
    assert(report.stepNodeCount >= 6);
    assert(report.sequenceCount == 1);
    assert(report.cycleSetCount == 1);

    std::array<uint8_t, 4096> bytes{};
    const auto encoded = asset_codec::encode(captured, bytes.data(), bytes.size());
    assert(encoded.ok());
    assert(encoded.bytesWritten > 0);

    SequencerStepGraphPreset decoded{};
    SequencerGraphAssetReport decodeReport{};
    assert(asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        &decodeReport
    ));
    assert(decodeReport.ok());
    assert(decoded.rootContext);
    assert(decoded.rootValuesValid);

    SequencerState target;
    target.pattern.setContentLength(8);
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
    source.pattern.setContentLength(8);
    const auto sourceMicro = createMicroSequence(source.pattern, rootStepNodeId(1), 2);
    assert(sourceMicro.ok);
    assert(enterMicroSequenceContentView(source, rootStepNodeId(1), sourceMicro.id));
    const auto sourceChildNode = activeContentStepNodeId(source, 1);
    assert(sourceChildNode != StepSequencerGraphLimits::INVALID_ID);
    const auto localChord = makeChord(
        4,
        StepSequencerChordHarmony::Suspended,
        StepSequencerChordVoicing::Open,
        3,
        12,
        -8
    );
    assert(setNodeChordSpec(source.pattern, sourceChildNode, localChord));
    assert(setNodeNoteOffset(source.pattern, sourceChildNode, -5));
    assert(setNodeNudgeOffset(source.pattern, sourceChildNode, 14));
    assert(setNodeLocalVariationRange(source.pattern, sourceChildNode, StepProperty::NUDGE, 11));

    const auto nestedMicro = createMicroSequence(source.pattern, sourceChildNode, 2);
    assert(nestedMicro.ok);

    SequencerStepGraphPreset captured{};
    SequencerGraphAssetReport report{};
    assert(captureStepGraphPreset(source, 1, {}, captured, &report));
    assert(report.ok());
    assert(!captured.rootContext);
    assert(!captured.rootValuesValid);
    assert(reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_GRAPH_PAYLOAD));
    assert(!reportHas(report, core::state::sequencer::SEQUENCER_GRAPH_ASSET_REPORT_ROOT_VALUES));

    std::array<uint8_t, 4096> bytes{};
    const auto encoded = asset_codec::encode(captured, bytes.data(), bytes.size());
    assert(encoded.ok());

    SequencerStepGraphPreset decoded{};
    assert(asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        nullptr
    ));
    assert(!decoded.rootContext);

    SequencerState target;
    target.pattern.setContentLength(8);
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
    rootSource.pattern.setContentLength(8);
    SequencerStepGraphPreset rootPreset{};
    SequencerGraphAssetReport report{};
    assert(captureStepGraphPreset(
        rootSource,
        0,
        {},
        rootPreset,
        &report
    ));

    SequencerState childTarget;
    childTarget.pattern.setContentLength(8);
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
    assert(!asset_codec::decode(
        tiny.data(),
        tiny.size(),
        decoded,
        &report
    ));
    assert(report.status == SequencerGraphAssetStatus::INVALID_ARGUMENT);

    SequencerState source;
    source.pattern.setContentLength(8);
    SequencerStepGraphPreset preset{};
    assert(captureStepGraphPreset(source, 0, {}, preset, nullptr));
    std::array<uint8_t, 512> bytes{};
    const auto encoded = asset_codec::encode(preset, bytes.data(), bytes.size());
    assert(encoded.ok());
    bytes[0] ^= 0xFFU;
    assert(!asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));
    assert(report.status == SequencerGraphAssetStatus::INVALID_FORMAT);

    std::cout << "[PASS] test_decode_rejects_invalid_buffers\n";
}

void test_pitch_policy_is_asset_level_and_unknown_node_flags_are_rejected() {
    SequencerState source;
    source.pattern.setContentLength(8);
    source.pattern.setPitchEditMode(SequencerPitchEditMode::FOLLOW_SCALE);
    const auto root = rootStepNodeId(0);
    const auto micro = createMicroSequence(source.pattern, root, 2);
    assert(micro.ok);

    auto* graph = source.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const uint16_t relativeChild = sequence->firstStepNode;
    assert(setNodeNoteOffset(source.pattern, relativeChild, 1));

    SequencerStepGraphPreset preset{};
    const auto sourceScale = constrainedScale();
    assert(captureStepGraphPreset(source, 0, sourceScale, preset, nullptr));
    assert(
        preset.scalePolicy ==
        SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
    );
    assert(preset.sourceScale.root == sourceScale.root);
    assert(preset.sourceScale.type == sourceScale.type);
    assert(preset.sourceScale.mode == sourceScale.mode);
    assert(!setStepGraphPresetMetadata(
        preset,
        "invalid-relative",
        "Invalid relative",
        SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE,
        {}
    ));
    assert(std::strcmp(preset.technicalId, "unsaved") == 0);
    assert(setStepGraphPresetMetadata(
        preset,
        "scale-policy",
        "Scale policy",
        SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE,
        sourceScale
    ));

    std::array<uint8_t, 4096> bytes{};
    const auto encoded = asset_codec::encode(preset, bytes.data(), bytes.size());
    assert(encoded.ok());

    SequencerStepGraphPreset decoded{};
    SequencerGraphAssetReport report{};
    assert(asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));
    assert(
        decoded.scalePolicy ==
        SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
    );

    auto invalidPreset = preset;
    invalidPreset.graph.stepNodes[0].flags = static_cast<uint16_t>(
        invalidPreset.graph.stepNodes[0].flags | (1U << 11)
    );
    assert(!asset_codec::encode(
        invalidPreset,
        bytes.data(),
        bytes.size()
    ).ok());

    invalidPreset = preset;
    invalidPreset.graph.stepNodes[0].chordSpec.harmonyData =
        static_cast<uint8_t>(
            invalidPreset.graph.stepNodes[0].chordSpec.harmonyData |
            0x80U
        );
    assert(!asset_codec::encode(
        invalidPreset,
        bytes.data(),
        bytes.size()
    ).ok());

    auto invalidBytes = bytes;
    const size_t firstNodeOffset =
        asset_codec::HEADER_SIZE +
        static_cast<size_t>(preset.graph.sequenceCount) *
            core::persistence::sequencer_graph_record_codec::
                SEQUENCE_RECORD_SIZE;
    invalidBytes[firstNodeOffset + 1U] = static_cast<uint8_t>(
        invalidBytes[firstNodeOffset + 1U] | 0x08U
    );
    assert(!asset_codec::decode(
        invalidBytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));

    invalidBytes = bytes;
    constexpr size_t CHORD_MODE_RECORD_OFFSET = 18U;
    invalidBytes[firstNodeOffset + CHORD_MODE_RECORD_OFFSET] = 0xFFU;
    assert(!asset_codec::decode(
        invalidBytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));

    invalidBytes = bytes;
    constexpr size_t CHORD_HARMONY_RECORD_OFFSET = 20U;
    invalidBytes[firstNodeOffset + CHORD_HARMONY_RECORD_OFFSET] =
        static_cast<uint8_t>(
            invalidBytes[firstNodeOffset + CHORD_HARMONY_RECORD_OFFSET] |
            0x80U
        );
    assert(!asset_codec::decode(
        invalidBytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));

    std::cout
        << "[PASS] Asset-level pitch policy and canonical nodes are strict\n";
}

void test_previous_format_is_rejected_strictly() {
    SequencerState source;
    source.pattern.setContentLength(8);
    source.pattern.setPitchEditMode(SequencerPitchEditMode::FOLLOW_SCALE);
    assert(setNodeNoteOffset(source.pattern, rootStepNodeId(0), 1));

    SequencerStepGraphPreset preset{};
    const auto sourceScale = constrainedScale();
    assert(captureStepGraphPreset(source, 0, sourceScale, preset, nullptr));
    assert(setStepGraphPresetMetadata(
        preset,
        "previous-fixture",
        "Previous fixture",
        SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE,
        sourceScale
    ));
    std::array<uint8_t, 4096> bytes{};
    const auto encoded = asset_codec::encode(preset, bytes.data(), bytes.size());
    assert(encoded.ok());

    bytes[4] = static_cast<uint8_t>(
        SequencerStepGraphPreset::CURRENT_FORMAT_VERSION - 1U
    );

    SequencerStepGraphPreset decoded{};
    SequencerGraphAssetReport report{};
    assert(!asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));
    assert(report.status == SequencerGraphAssetStatus::UNSUPPORTED_VERSION);

    std::cout << "[PASS] test_previous_format_is_rejected_strictly\n";
}

void test_current_semantic_chord_roundtrip_rejects_previous_version() {
    SequencerState source;
    source.pattern.setContentLength(8);
    auto semantic = StepSequencerChordSpec::semantic(
        oc::note::sequencer::StepSequencerChordHarmony::Custom,
        8,
        oc::note::sequencer::StepSequencerChordVoicing::Open,
        1,
        oc::note::sequencer::StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    constexpr std::array<uint8_t, 8> intervals{
        0U, 3U, 5U, 8U, 12U, 17U, 24U, 31U,
    };
    for (uint8_t voice = 7U; voice > 0U; --voice) {
        semantic.setCustomInterval(voice, intervals[voice]);
    }
    semantic.strum = 21;
    semantic.velocityCurve = -7;
    assert(setNodeChordSpec(source.pattern, rootStepNodeId(0), semantic));

    SequencerStepGraphPreset preset{};
    assert(captureStepGraphPreset(source, 0, {}, preset, nullptr));
    std::array<uint8_t, 4096> bytes{};
    const auto encoded = asset_codec::encode(preset, bytes.data(), bytes.size());
    assert(encoded.ok());
    assert(bytes[4] == SequencerStepGraphPreset::CURRENT_FORMAT_VERSION);

    SequencerStepGraphPreset decoded{};
    assert(asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        nullptr
    ));
    const auto* node = decoded.graph.stepNode(SequencerStepGraphPreset::ASSET_ROOT_NODE_ID);
    assert(node != nullptr);
    assertSameChordSpec(node->chordSpec, semantic);
    assert(node->chordSpec.isCustom());
    assert(node->chordSpec.voices() == intervals.size());
    for (uint8_t voice = 0U; voice < intervals.size(); ++voice) {
        assert(node->chordSpec.customInterval(voice) == intervals[voice]);
    }

    bytes[4] = static_cast<uint8_t>(
        SequencerStepGraphPreset::CURRENT_FORMAT_VERSION - 1U
    );
    assert(!asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        nullptr
    ));

    std::cout << "[PASS] test_current_semantic_chord_roundtrip_rejects_previous_version\n";
}

void test_current_metadata_is_bounded_valid_utf8_and_nonempty() {
    SequencerState source;
    source.pattern.setContentLength(8);
    SequencerStepGraphPreset preset{};
    assert(captureStepGraphPreset(source, 0, {}, preset, nullptr));

    std::array<uint8_t, 1024> bytes{};
    auto invalid = preset;
    invalid.semanticName[0] = '\0';
    assert(!asset_codec::encode(invalid, bytes.data(), bytes.size()).ok());

    invalid = preset;
    std::memset(invalid.semanticName, 'a', sizeof(invalid.semanticName));
    assert(!asset_codec::encode(invalid, bytes.data(), bytes.size()).ok());

    invalid = preset;
    invalid.semanticName[0] = '\x01';
    invalid.semanticName[1] = '\0';
    assert(!asset_codec::encode(invalid, bytes.data(), bytes.size()).ok());

    invalid = preset;
    invalid.semanticName[0] = static_cast<char>(0xC3);
    invalid.semanticName[1] = '(';
    invalid.semanticName[2] = '\0';
    assert(!asset_codec::encode(invalid, bytes.data(), bytes.size()).ok());

    assert(setStepGraphPresetMetadata(
        preset,
        "quoted-name",
        "Nom \"A\" \\ scene",
        SequencerStepGraphPreset::ScalePolicy::CHROMATIC,
        {}
    ));
    const auto encoded = asset_codec::encode(preset, bytes.data(), bytes.size());
    assert(encoded.ok());
    SequencerStepGraphPreset decoded{};
    assert(asset_codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decoded,
        nullptr
    ));
    assert(std::strcmp(decoded.semanticName, "Nom \"A\" \\ scene") == 0);

    constexpr size_t semanticOffset =
        asset_codec::BASE_HEADER_SIZE + 4U +
        SequencerStepGraphPreset::TECHNICAL_ID_SIZE;
    auto corruptBytes = bytes;
    corruptBytes[semanticOffset] = static_cast<uint8_t>(0xC3);
    corruptBytes[semanticOffset + 1U] = static_cast<uint8_t>('(');
    corruptBytes[semanticOffset + 2U] = 0;
    assert(!asset_codec::decode(
        corruptBytes.data(),
        encoded.bytesWritten,
        decoded,
        nullptr
    ));

    std::cout << "[PASS] test_current_metadata_is_bounded_valid_utf8_and_nonempty\n";
}

void test_pitch_metadata_and_root_values_have_one_canonical_encoding() {
    SequencerState source;
    source.pattern.setContentLength(8);
    source.pattern.setPitchEditMode(SequencerPitchEditMode::CHROMATIC);

    SequencerStepGraphPreset preset{};
    const auto ignoredConstrainedScale = constrainedScale();
    assert(captureStepGraphPreset(
        source,
        0,
        ignoredConstrainedScale,
        preset,
        nullptr
    ));
    assert(
        preset.scalePolicy ==
        SequencerStepGraphPreset::ScalePolicy::CHROMATIC
    );
    assert(preset.sourceScale.root == 0);
    assert(preset.sourceScale.type == StepSequencerScaleType::Chromatic);
    assert(preset.sourceScale.mode == StepSequencerScaleConstraintMode::Free);

    assert(setStepGraphPresetMetadata(
        preset,
        "canonical",
        "Canonical",
        SequencerStepGraphPreset::ScalePolicy::CHROMATIC,
        ignoredConstrainedScale
    ));
    assert(preset.sourceScale.root == 0);
    assert(preset.sourceScale.type == StepSequencerScaleType::Chromatic);
    assert(preset.sourceScale.mode == StepSequencerScaleConstraintMode::Free);

    std::array<uint8_t, 1024> bytes{};
    const auto encoded = asset_codec::encode(preset, bytes.data(), bytes.size());
    assert(encoded.ok());

    auto invalidPreset = preset;
    invalidPreset.sourceScale = ignoredConstrainedScale;
    assert(!asset_codec::encode(
        invalidPreset,
        bytes.data(),
        bytes.size()
    ).ok());

    SequencerStepGraphPreset decoded{};
    SequencerGraphAssetReport report{};
    auto invalidBytes = bytes;
    constexpr size_t sourceScaleRootOffset =
        asset_codec::BASE_HEADER_SIZE + 1U;
    invalidBytes[sourceScaleRootOffset] = 1U;
    assert(!asset_codec::decode(
        invalidBytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));
    assert(report.status == SequencerGraphAssetStatus::INVALID_FORMAT);

    invalidBytes = bytes;
    constexpr size_t rootNoteOffset = 9U;
    invalidBytes[rootNoteOffset] = 200U;
    assert(!asset_codec::decode(
        invalidBytes.data(),
        encoded.bytesWritten,
        decoded,
        &report
    ));
    assert(report.status == SequencerGraphAssetStatus::INVALID_FORMAT);

    std::cout
        << "[PASS] Pitch metadata and root values use one canonical encoding\n";
}

}  // namespace

int main() {
    test_root_step_graph_preset_roundtrip_preserves_nested_payload();
    test_child_step_graph_preset_roundtrip_preserves_local_payload_only();
    test_context_mismatch_is_reported();
    test_decode_rejects_invalid_buffers();
    test_pitch_policy_is_asset_level_and_unknown_node_flags_are_rejected();
    test_previous_format_is_rejected_strictly();
    test_current_semantic_chord_roundtrip_rejects_previous_version();
    test_current_metadata_is_bounded_valid_utf8_and_nonempty();
    test_pitch_metadata_and_root_values_have_one_canonical_encoding();
    std::cout << "[PASS] SequencerGraphAssetCodec tests\n";
    return 0;
}
