#include <cassert>
#include <iostream>

#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/ui/sequencer/StepContentBadgeProjection.hpp"

namespace {

using core::state::sequencer::SequencerState;
using core::state::sequencer::createCycleStateSet;
using core::state::sequencer::createMicroSequence;
using core::state::sequencer::enterCycleStatesContentView;
using core::state::sequencer::enterMicroSequenceContentView;
using core::state::sequencer::rootStepNodeId;
using core::state::sequencer::setNodeEnabledOverride;
using core::state::sequencer::setNodeChordSpec;
using core::state::sequencer::setNodeNoteOffset;
using core::state::sequencer::setNodeLocalVariationRange;
using core::state::sequencer::StepProperty;
using core::ui::sequencer::grid::buildStepContentBadgeProjection;
using core::ui::sequencer::grid::buildStepContentBadgeProjectionForNode;
using core::ui::sequencer::grid::mergeExpandedTelemetryChordBadgeForNode;

void test_projects_root_step_content_badges() {
    core::state::sequencer::SequencerPatternState pattern;

    assert(createMicroSequence(pattern, rootStepNodeId(1), 2).ok);
    assert(createCycleStateSet(pattern, rootStepNodeId(2), 4).ok);
    assert(createMicroSequence(pattern, rootStepNodeId(3), 2).ok);
    assert(createCycleStateSet(pattern, rootStepNodeId(3), 4).ok);

    auto badges = buildStepContentBadgeProjection(pattern, 0);
    assert(!badges.microSequence);
    assert(!badges.cycleStates);
    assert(!badges.chord);

    badges = buildStepContentBadgeProjection(pattern, 1);
    assert(badges.microSequence);
    assert(!badges.cycleStates);
    assert(!badges.chord);

    badges = buildStepContentBadgeProjection(pattern, 2);
    assert(!badges.microSequence);
    assert(badges.cycleStates);
    assert(!badges.chord);

    badges = buildStepContentBadgeProjection(pattern, 3);
    assert(badges.microSequence);
    assert(badges.cycleStates);
    assert(!badges.chord);

    std::cout << "[PASS] test_projects_root_step_content_badges\n";
}

void test_invalid_or_missing_graph_has_no_badges() {
    core::state::sequencer::SequencerPatternState pattern;

    auto badges = buildStepContentBadgeProjection(pattern, 1);
    assert(!badges.microSequence);
    assert(!badges.cycleStates);
    assert(!badges.chord);

    assert(createMicroSequence(pattern, rootStepNodeId(1), 2).ok);
    badges = buildStepContentBadgeProjection(pattern, SequencerState::MAX_STEPS);
    assert(!badges.microSequence);
    assert(!badges.cycleStates);
    assert(!badges.chord);

    std::cout << "[PASS] test_invalid_or_missing_graph_has_no_badges\n";
}

void test_projects_child_context_resolved_values_and_badges() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[1] = 60;

    const auto micro = createMicroSequence(sequencer.pattern, rootStepNodeId(1), 2);
    assert(micro.ok);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto microNode = sequence->firstStepNode;
    assert(setNodeNoteOffset(sequencer.pattern, microNode, 2));
    assert(createCycleStateSet(sequencer.pattern, microNode, 4).ok);

    sequencer.focusedStep.set(1);
    assert(enterMicroSequenceContentView(sequencer, rootStepNodeId(1), micro.id));

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        0,
        {}
    );
    assert(projection.valid);
    assert(projection.parentNote == 60);
    assert(projection.note == 62);

    const auto badges = buildStepContentBadgeProjectionForNode(
        sequencer.pattern,
        projection.nodeId
    );
    assert(badges.cycleStates);
    assert(!badges.microSequence);
    assert(!badges.chord);

    std::cout << "[PASS] test_projects_child_context_resolved_values_and_badges\n";
}

void test_projects_chord_badge_for_local_chord_step() {
    core::state::sequencer::SequencerPatternState pattern;

    oc::note::sequencer::StepSequencerChordSpec spec{};
    spec.voiceCount = 5;
    assert(setNodeChordSpec(pattern, rootStepNodeId(0), spec));

    auto badges = buildStepContentBadgeProjection(pattern, 0);
    assert(badges.chord);
    assert(badges.chordVoiceCount == 5);
    assert(badges.chordSource == oc::note::sequencer::StepSequencerChordSource::Local);
    assert(!badges.microSequence);
    assert(!badges.cycleStates);

    std::cout << "[PASS] test_projects_chord_badge_for_local_chord_step\n";
}

void test_child_grid_uses_runtime_chord_badge_for_inherited_chord() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);
    sequencer.probabilityCycleMask.setBit(0, true);
    sequencer.playheadStep.set(0);
    sequencer.playheadStepTickOffset.set(0);

    oc::note::sequencer::StepSequencerChordSpec rootChord{};
    rootChord.voiceCount = 4;
    assert(setNodeChordSpec(sequencer.pattern, rootStepNodeId(0), rootChord));

    const auto micro = createMicroSequence(sequencer.pattern, rootStepNodeId(0), 2);
    assert(micro.ok);
    const auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto childNode = sequence->firstStepNode;

    assert(enterMicroSequenceContentView(sequencer, rootStepNodeId(0), micro.id));

    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = 0;
    variation.triggered = true;
    variation.base = {.note = 60, .velocity = 64, .gate = 100, .nudge = 0};
    variation.resolved = variation.base;
    sequencer.expandedVariationTelemetry.reset();
    sequencer.expandedVariationTelemetry.valid = true;
    sequencer.expandedVariationTelemetry.rootStepIndex = 0;
    sequencer.expandedVariationTelemetry.store(
        0,
        childNode,
        0,
        24,
        variation,
        oc::note::sequencer::StepSequencerChordSource::Inherited,
        0,
        4,
        0,
        true
    );

    auto badges = buildStepContentBadgeProjectionForNode(sequencer.pattern, childNode);
    assert(!badges.chord);
    assert(mergeExpandedTelemetryChordBadgeForNode(
        badges,
        sequencer.expandedVariationTelemetry,
        childNode,
        sequencer.playheadStep.get(),
        sequencer.playheadStepTickOffset.get()
    ));
    assert(badges.chord);
    assert(badges.chordVoiceCount == 4);
    assert(badges.chordSource ==
           oc::note::sequencer::StepSequencerChordSource::Inherited);

    std::cout << "[PASS] test_child_grid_uses_runtime_chord_badge_for_inherited_chord\n";
}

void test_parent_grid_summarizes_final_child_pitch() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);

    sequencer.pattern.note[1] = 60;
    const auto micro = createMicroSequence(sequencer.pattern, rootStepNodeId(1), 2);
    assert(micro.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* microSequence = graph->sequence(micro.id);
    assert(microSequence != nullptr);
    const auto microNode = microSequence->firstStepNode;
    assert(setNodeNoteOffset(sequencer.pattern, microNode, 2));

    uint8_t childNote = 0;
    auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        1,
        {}
    );
    assert(core::state::sequencer::resolveRepresentativeChildContentNote(
        sequencer,
        projection,
        {},
        childNote
    ));
    assert(childNote == 62);

    sequencer.pattern.note[2] = 60;
    const auto cycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(2), 4);
    assert(cycle.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    assert(setNodeNoteOffset(
        sequencer.pattern,
        static_cast<uint16_t>(cycleSet->firstStateNode + 1U),
        3
    ));
    sequencer.probabilityCycleIndex = 1;

    projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        2,
        {}
    );
    assert(core::state::sequencer::resolveRepresentativeChildContentNote(
        sequencer,
        projection,
        {},
        childNote
    ));
    assert(childNote == 63);

    sequencer.pattern.note[3] = 60;
    const auto nestedMicro = createMicroSequence(sequencer.pattern, rootStepNodeId(3), 2);
    assert(nestedMicro.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* nestedSequence = graph->sequence(nestedMicro.id);
    assert(nestedSequence != nullptr);
    const auto nestedMicroNode = nestedSequence->firstStepNode;
    assert(setNodeNoteOffset(sequencer.pattern, nestedMicroNode, 2));
    const auto nestedCycle = createCycleStateSet(sequencer.pattern, nestedMicroNode, 2);
    assert(nestedCycle.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* nestedCycleSet = graph->cycleSet(nestedCycle.id);
    assert(nestedCycleSet != nullptr);
    assert(setNodeNoteOffset(
        sequencer.pattern,
        static_cast<uint16_t>(nestedCycleSet->firstStateNode + 1U),
        3
    ));
    sequencer.probabilityCycleIndex = 1;

    projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        3,
        {}
    );
    assert(core::state::sequencer::resolveRepresentativeChildContentNote(
        sequencer,
        projection,
        {},
        childNote
    ));
    assert(childNote == 65);

    std::cout << "[PASS] test_parent_grid_summarizes_final_child_pitch\n";
}

void test_parent_tile_displays_final_child_pitch_across_nested_cycles() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);

    const auto rootCycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(0), 2);
    assert(rootCycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* rootCycleSet = graph->cycleSet(rootCycle.id);
    assert(rootCycleSet != nullptr);
    const auto rootStateNode = static_cast<uint16_t>(rootCycleSet->firstStateNode + 1U);
    assert(setNodeNoteOffset(sequencer.pattern, rootStateNode, 1));

    const auto secondCycle = createCycleStateSet(sequencer.pattern, rootStateNode, 2);
    assert(secondCycle.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* secondCycleSet = graph->cycleSet(secondCycle.id);
    assert(secondCycleSet != nullptr);
    const auto secondStateNode = static_cast<uint16_t>(secondCycleSet->firstStateNode + 1U);
    assert(setNodeNoteOffset(sequencer.pattern, secondStateNode, 2));

    const auto thirdCycle = createCycleStateSet(sequencer.pattern, secondStateNode, 2);
    assert(thirdCycle.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* thirdCycleSet = graph->cycleSet(thirdCycle.id);
    assert(thirdCycleSet != nullptr);
    const auto thirdStateNode = static_cast<uint16_t>(thirdCycleSet->firstStateNode + 1U);
    assert(setNodeNoteOffset(sequencer.pattern, thirdStateNode, 3));

    sequencer.probabilityCycleIndex = 7;

    uint8_t childNote = 0;
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        0,
        {}
    );
    assert(core::state::sequencer::resolveRepresentativeChildContentNote(
        sequencer,
        projection,
        {},
        childNote
    ));
    assert(childNote == 66);

    std::cout << "[PASS] test_parent_tile_displays_final_child_pitch_across_nested_cycles\n";
}

void test_child_grid_summarizes_intermediate_child_pitch() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;

    const auto cycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(0), 2);
    assert(cycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    const auto stateNode = static_cast<uint16_t>(cycleSet->firstStateNode + 1U);
    assert(setNodeNoteOffset(sequencer.pattern, stateNode, 2));

    const auto nestedCycle = createCycleStateSet(sequencer.pattern, stateNode, 2);
    assert(nestedCycle.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* nestedCycleSet = graph->cycleSet(nestedCycle.id);
    assert(nestedCycleSet != nullptr);
    assert(setNodeNoteOffset(
        sequencer.pattern,
        static_cast<uint16_t>(nestedCycleSet->firstStateNode + 1U),
        3
    ));

    sequencer.focusedStep.set(0);
    assert(enterCycleStatesContentView(sequencer, rootStepNodeId(0), cycle.id));
    sequencer.probabilityCycleIndex = 1;

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        1,
        {}
    );
    core::state::sequencer::SequencerChildContentSummary summary{};
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.note == 62);

    sequencer.probabilityCycleIndex = 3;
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.note == 65);

    std::cout << "[PASS] test_child_grid_summarizes_intermediate_child_pitch\n";
}

void test_child_summary_reports_representative_local_variation() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);

    const auto cycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(0), 2);
    assert(cycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    const auto stateNode = static_cast<uint16_t>(cycleSet->firstStateNode + 1U);
    assert(setNodeNoteOffset(sequencer.pattern, stateNode, 2));
    assert(setNodeLocalVariationRange(sequencer.pattern, stateNode, StepProperty::NOTE, 5));

    sequencer.probabilityCycleIndex = 1;
    auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        0,
        {}
    );
    core::state::sequencer::SequencerChildContentSummary summary{};
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.nodeId == stateNode);
    assert(summary.note == 62);
    assert(summary.localVariation.pitchSemitones == 5);

    const auto micro = createMicroSequence(sequencer.pattern, stateNode, 2);
    assert(micro.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto microNode = sequence->firstStepNode;
    assert(setNodeNoteOffset(sequencer.pattern, microNode, 3));
    assert(setNodeLocalVariationRange(sequencer.pattern, microNode, StepProperty::VELOCITY, 12));

    projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        0,
        {}
    );
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.nodeId == microNode);
    assert(summary.note == 65);
    assert(summary.localVariation.pitchSemitones == 5);
    assert(summary.localVariation.velocity == 12);

    std::cout << "[PASS] test_child_summary_reports_representative_local_variation\n";
}

void test_intermediate_cycle_summary_uses_owner_activation_count() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[2] = 60;

    const auto cycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(2), 4);
    assert(cycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    const auto stateNode = cycleSet->firstStateNode;

    const auto nestedCycle = createCycleStateSet(sequencer.pattern, stateNode, 4);
    assert(nestedCycle.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* nestedCycleSet = graph->cycleSet(nestedCycle.id);
    assert(nestedCycleSet != nullptr);
    assert(setNodeNoteOffset(sequencer.pattern, nestedCycleSet->firstStateNode, 3));
    assert(setNodeNoteOffset(
        sequencer.pattern,
        static_cast<uint16_t>(nestedCycleSet->firstStateNode + 2U),
        -3
    ));

    sequencer.focusedStep.set(2);
    assert(enterCycleStatesContentView(sequencer, rootStepNodeId(2), cycle.id));

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        0,
        {}
    );
    core::state::sequencer::SequencerChildContentSummary summary{};

    sequencer.probabilityCycleIndex = 0;
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.note == 63);

    sequencer.probabilityCycleIndex = 4;
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.note == 60);

    sequencer.probabilityCycleIndex = 8;
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.note == 57);

    std::cout << "[PASS] test_intermediate_cycle_summary_uses_owner_activation_count\n";
}

void test_nested_child_playhead_follows_active_owner_path() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);
    sequencer.probabilityCycleMask.setBit(0, true);
    sequencer.playheadStep.set(0);
    sequencer.playheadStepTicks = 24;
    sequencer.playheadStepTickOffset.set(0);

    const auto cycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(0), 2);
    assert(cycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    const auto stateNode = static_cast<uint16_t>(cycleSet->firstStateNode + 1U);

    const auto nestedCycle = createCycleStateSet(sequencer.pattern, stateNode, 2);
    assert(nestedCycle.ok);

    sequencer.focusedStep.set(0);
    assert(enterCycleStatesContentView(sequencer, rootStepNodeId(0), cycle.id));
    sequencer.focusedStep.set(1);
    assert(enterCycleStatesContentView(sequencer, stateNode, nestedCycle.id));
    assert(core::state::sequencer::activeContentDepth(sequencer) == 2);

    sequencer.probabilityCycleIndex = 0;
    auto playback = core::state::sequencer::resolveActiveContentPlaybackProjection(
        sequencer,
        {}
    );
    assert(!playback.active);

    sequencer.probabilityCycleIndex = 1;
    playback = core::state::sequencer::resolveActiveContentPlaybackProjection(
        sequencer,
        {}
    );
    assert(playback.active);
    assert(playback.step == 0);

    sequencer.probabilityCycleIndex = 3;
    playback = core::state::sequencer::resolveActiveContentPlaybackProjection(
        sequencer,
        {}
    );
    assert(playback.active);
    assert(playback.step == 1);

    std::cout << "[PASS] test_nested_child_playhead_follows_active_owner_path\n";
}

void test_child_disabled_state_is_reported_to_parent_summary() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);

    const auto cycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(0), 4);
    assert(cycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    const auto thirdStateNode = static_cast<uint16_t>(cycleSet->firstStateNode + 2U);
    assert(core::state::sequencer::setNodeEnabledOverride(
        sequencer.pattern,
        thirdStateNode,
        false
    ));

    sequencer.probabilityCycleIndex = 2;

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        0,
        {}
    );
    core::state::sequencer::SequencerChildContentSummary summary{};
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(!summary.enabled);
    assert(summary.note == 60);

    std::cout << "[PASS] test_child_disabled_state_is_reported_to_parent_summary\n";
}

void test_child_playhead_remains_visible_when_selected_state_is_disabled() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);
    sequencer.probabilityCycleMask.setBit(0, true);
    sequencer.playheadStep.set(0);
    sequencer.playheadStepTicks = 24;
    sequencer.playheadStepTickOffset.set(0);

    const auto cycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(0), 4);
    assert(cycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    const auto thirdStateNode = static_cast<uint16_t>(cycleSet->firstStateNode + 2U);
    assert(core::state::sequencer::setNodeEnabledOverride(
        sequencer.pattern,
        thirdStateNode,
        false
    ));

    sequencer.focusedStep.set(0);
    assert(enterCycleStatesContentView(sequencer, rootStepNodeId(0), cycle.id));
    sequencer.probabilityCycleIndex = 2;

    const auto playback = core::state::sequencer::resolveActiveContentPlaybackProjection(
        sequencer,
        {}
    );
    assert(playback.visible);
    assert(!playback.active);
    assert(playback.step == 2);

    std::cout << "[PASS] test_child_playhead_remains_visible_when_selected_state_is_disabled\n";
}

void test_parent_summary_uses_current_micro_substep_runtime_note() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);
    sequencer.probabilityCycleMask.setBit(0, true);
    sequencer.playheadStep.set(0);
    sequencer.playheadStepTicks = 24;

    const auto micro = createMicroSequence(sequencer.pattern, rootStepNodeId(0), 2);
    assert(micro.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto firstNode = sequence->firstStepNode;
    const auto secondNode = static_cast<uint16_t>(sequence->firstStepNode + 1U);
    assert(setNodeEnabledOverride(sequencer.pattern, firstNode, false));
    assert(setNodeNoteOffset(sequencer.pattern, secondNode, 7));

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        0,
        {}
    );
    assert(projection.valid);

    sequencer.playheadStepTickOffset.set(0);
    core::state::sequencer::SequencerChildContentSummary summary{};
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.nodeId == firstNode);
    assert(!summary.enabled);

    sequencer.playheadStepTickOffset.set(12);
    assert(core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        {},
        summary
    ));
    assert(summary.nodeId == secondNode);
    assert(summary.enabled);
    assert(summary.note == 67);

    std::cout << "[PASS] test_parent_summary_uses_current_micro_substep_runtime_note\n";
}

void test_resolved_projection_reports_current_child_runtime_note() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);
    sequencer.probabilityCycleMask.setBit(0, true);
    sequencer.playheadStep.set(0);
    sequencer.playheadStepTicks = 24;

    const auto micro = createMicroSequence(sequencer.pattern, rootStepNodeId(0), 2);
    assert(micro.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto firstNode = sequence->firstStepNode;
    const auto secondNode = static_cast<uint16_t>(sequence->firstStepNode + 1U);
    assert(setNodeEnabledOverride(sequencer.pattern, firstNode, false));
    assert(setNodeNoteOffset(sequencer.pattern, secondNode, 7));

    sequencer.playheadStepTickOffset.set(12);
    const auto context =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            {},
            StepProperty::NOTE
        );
    const auto step = core::state::sequencer::buildSequencerResolvedStepDisplayState(
        context,
        0,
        false
    );

    assert(step.inPattern);
    assert(step.enabled);
    assert(step.childPitchSummaryVisible);
    assert(step.childPitchSummaryNote == 67);
    assert(step.variation.visible);
    assert(step.variation.resolved.resolved.note == 67);

    std::cout << "[PASS] test_resolved_projection_reports_current_child_runtime_note\n";
}

void test_resolved_projection_reports_runtime_inherited_chord_badge() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);
    sequencer.probabilityCycleMask.setBit(0, true);
    sequencer.playheadStep.set(0);
    sequencer.playheadStepTickOffset.set(0);

    oc::note::sequencer::StepSequencerChordSpec rootChord{};
    rootChord.voiceCount = 4;
    assert(setNodeChordSpec(sequencer.pattern, rootStepNodeId(0), rootChord));

    const auto micro = createMicroSequence(sequencer.pattern, rootStepNodeId(0), 2);
    assert(micro.ok);
    const auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto childNode = sequence->firstStepNode;

    assert(enterMicroSequenceContentView(sequencer, rootStepNodeId(0), micro.id));

    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = 0;
    variation.triggered = true;
    variation.base = {.note = 60, .velocity = 64, .gate = 100, .nudge = 0};
    variation.resolved = variation.base;
    sequencer.expandedVariationTelemetry.reset();
    sequencer.expandedVariationTelemetry.valid = true;
    sequencer.expandedVariationTelemetry.rootStepIndex = 0;
    sequencer.expandedVariationTelemetry.store(
        0,
        childNode,
        0,
        24,
        variation,
        oc::note::sequencer::StepSequencerChordSource::Inherited,
        0,
        4,
        0,
        true
    );

    const auto context =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            {},
            StepProperty::NOTE
        );
    const auto step = core::state::sequencer::buildSequencerResolvedStepDisplayState(
        context,
        0,
        false
    );
    auto badges = buildStepContentBadgeProjectionForNode(sequencer.pattern, step.nodeId);
    assert(mergeExpandedTelemetryChordBadgeForNode(
        badges,
        sequencer.expandedVariationTelemetry,
        step.runtimeNodeId,
        sequencer.playheadStep.get(),
        sequencer.playheadStepTickOffset.get()
    ));

    assert(step.inPattern);
    assert(badges.chord);
    assert(badges.chordVoiceCount == 4);
    assert(badges.chordSource ==
           oc::note::sequencer::StepSequencerChordSource::Inherited);

    std::cout << "[PASS] test_resolved_projection_reports_runtime_inherited_chord_badge\n";
}

void test_resolved_projection_sums_pattern_and_local_random_preview() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);
    sequencer.pattern.note[0] = 60;
    sequencer.pattern.setEnabled(0, true);
    sequencer.activeStepProperty.set(StepProperty::NOTE);
    assert(sequencer.setVariationRangeForProperty(StepProperty::NOTE, 2));
    assert(setNodeLocalVariationRange(
        sequencer.pattern,
        rootStepNodeId(0),
        StepProperty::NOTE,
        3
    ));

    const auto context =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            {},
            StepProperty::NOTE
        );
    const auto step = core::state::sequencer::buildSequencerResolvedStepDisplayState(
        context,
        0,
        false
    );

    assert(step.inPattern);
    assert(step.variation.visible);
    assert(step.variation.rangeVisible);
    assert(step.variation.resolved.ranges.pitchSemitones == 5);

    std::cout << "[PASS] test_resolved_projection_sums_pattern_and_local_random_preview\n";
}

void test_ui_allows_three_child_content_levels_when_engine_depth_is_four() {
    SequencerState sequencer;
    sequencer.pattern.length.set(8);

    const auto rootCycle = createCycleStateSet(sequencer.pattern, rootStepNodeId(0), 2);
    assert(rootCycle.ok);
    auto* graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* rootCycleSet = graph->cycleSet(rootCycle.id);
    assert(rootCycleSet != nullptr);
    const auto firstStateNode = rootCycleSet->firstStateNode;

    const auto secondCycle = createCycleStateSet(sequencer.pattern, firstStateNode, 2);
    assert(secondCycle.ok);
    graph = sequencer.pattern.graph.get();
    assert(graph != nullptr);
    const auto* secondCycleSet = graph->cycleSet(secondCycle.id);
    assert(secondCycleSet != nullptr);
    const auto secondStateNode = secondCycleSet->firstStateNode;

    const auto thirdCycle = createCycleStateSet(sequencer.pattern, secondStateNode, 2);
    assert(thirdCycle.ok);

    sequencer.focusedStep.set(0);
    assert(enterCycleStatesContentView(sequencer, rootStepNodeId(0), rootCycle.id));
    sequencer.focusedStep.set(0);
    assert(enterCycleStatesContentView(sequencer, firstStateNode, secondCycle.id));
    sequencer.focusedStep.set(0);
    assert(enterCycleStatesContentView(sequencer, secondStateNode, thirdCycle.id));
    assert(core::state::sequencer::activeContentDepth(sequencer) == 3);

    std::cout << "[PASS] test_ui_allows_three_child_content_levels_when_engine_depth_is_four\n";
}

}  // namespace

int main() {
    test_projects_root_step_content_badges();
    test_invalid_or_missing_graph_has_no_badges();
    test_projects_child_context_resolved_values_and_badges();
    test_projects_chord_badge_for_local_chord_step();
    test_child_grid_uses_runtime_chord_badge_for_inherited_chord();
    test_parent_grid_summarizes_final_child_pitch();
    test_parent_tile_displays_final_child_pitch_across_nested_cycles();
    test_child_grid_summarizes_intermediate_child_pitch();
    test_child_summary_reports_representative_local_variation();
    test_intermediate_cycle_summary_uses_owner_activation_count();
    test_nested_child_playhead_follows_active_owner_path();
    test_child_disabled_state_is_reported_to_parent_summary();
    test_child_playhead_remains_visible_when_selected_state_is_disabled();
    test_parent_summary_uses_current_micro_substep_runtime_note();
    test_resolved_projection_reports_current_child_runtime_note();
    test_resolved_projection_reports_runtime_inherited_chord_badge();
    test_resolved_projection_sums_pattern_and_local_random_preview();
    test_ui_allows_three_child_content_levels_when_engine_depth_is_four();

    std::cout << "\nAll SequencerStepGridFrame tests passed.\n";
    return 0;
}
