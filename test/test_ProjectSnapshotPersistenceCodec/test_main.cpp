#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "../../src/persistence/ProjectStatePersistenceCodec.hpp"
#include "../../src/persistence/SequencerPersistenceEnvelope.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"
#include "../../src/state/sequencer/SequencerScaleState.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
using oc::note::sequencer::STEP_NODE_ENABLED_VALUE;
using oc::note::sequencer::STEP_NODE_GATE_OFFSET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::STEP_NODE_NUDGE_OFFSET;
using oc::note::sequencer::STEP_NODE_PROBABILITY_OFFSET;
using oc::note::sequencer::STEP_NODE_VELOCITY_OFFSET;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSpec;

constexpr size_t kProjectSnapshotScratchSize = 512U * 1024U;
static_assert(snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR > 0,
              "Stale-minor snapshot chunk tests need a previous minor version");

core::state::CoreState makeCoreState(test_support::CoreStorages& storages) {
    return core::state::CoreState{
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary,
    };
}

bool reportHas(const project_file::LoadReport& report, project_file::LoadCode code) {
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        if (report.items[i].code == code) return true;
    }
    return false;
}

bool sameScale(const StepSequencerScaleSettings& lhs,
               const StepSequencerScaleSettings& rhs) {
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

core::state::macro::MacroAutomationSlotAddress denseMacroAutomationAddress(uint8_t index) {
    return core::state::macro::MacroAutomationSlotAddress{
        .track = static_cast<uint8_t>(index / 5U),
        .page = static_cast<uint8_t>(index % 5U),
        .macro = static_cast<uint8_t>((index * 3U) % core::state::macro::MACRO_COUNT),
    };
}

float denseMacroAutomationDuration(uint8_t index) {
    return 4.0f + static_cast<float>(index % 4U) * 4.0f;
}

float denseMacroAutomationValue(uint8_t index, uint8_t point) {
    const uint8_t raw = static_cast<uint8_t>((index * 11U + point * 7U) % 128U);
    return static_cast<float>(raw) / 127.0f;
}

bool denseMacroAutomationHasModulation(uint8_t index) {
    return (index % 4U) == 0U;
}

float denseMacroAutomationWindowOffset(uint8_t index) {
    return (index % 2U) == 0U ? 1.0f : 0.0f;
}

float denseMacroModulationValue(uint8_t index, uint8_t point) {
    const int raw = static_cast<int>(index) * 3 + static_cast<int>(point) * 5 - 16;
    return static_cast<float>(raw) / 32.0f;
}

void configureDenseMacroAutomations(core::state::CoreState& state) {
    using namespace core::state::macro;

    constexpr uint8_t kAutomationCount = 15;
    constexpr uint8_t kPointCount = 6;

    for (uint8_t i = 0; i < kAutomationCount; ++i) {
        const auto address = denseMacroAutomationAddress(i);
        auto& page = state.pages.pageData(address.track, address.page);
        state.pages.tracks[address.track].setPageEnabled(address.page, true);
        page.setMacroActive(address.macro, true);
        page.cc[address.macro] = static_cast<uint8_t>(40U + i);
        page.values[address.macro] = denseMacroAutomationValue(i, 0);

        auto* slot = macroAutomationGetOrCreateSlot(state.pages.automation, address);
        assert(slot != nullptr);

        MacroAutomationLane lane;
        lane.durationBeats = denseMacroAutomationDuration(i);
        for (uint8_t point = 0; point < kPointCount; ++point) {
            const float beat =
                (lane.durationBeats * static_cast<float>(point)) /
                static_cast<float>(kPointCount - 1U);
            assert(macroAutomationAppendPoint(
                lane,
                beat,
                denseMacroAutomationValue(i, point)
            ));
        }
        assert(macroAutomationAssignAutomation(state.pages.automation, *slot, lane));
        const float windowOffset = denseMacroAutomationWindowOffset(i);
        assert(macroAutomationSetCurveWindowOffset(
            slot->automation,
            state.pages.automation.pointPool,
            windowOffset
        ) || windowOffset == 0.0f);

        if (denseMacroAutomationHasModulation(i)) {
            MacroModulationShape modulation;
            modulation.durationBeats = lane.durationBeats;
            for (uint8_t point = 0; point < 3; ++point) {
                const float beat =
                    (modulation.durationBeats * static_cast<float>(point)) / 2.0f;
                assert(macroModulationAppendPoint(
                    modulation,
                    beat,
                    denseMacroModulationValue(i, point)
                ));
            }
            assert(macroAutomationAssignModulation(
                state.pages.automation,
                *slot,
                modulation
            ));
            slot->modulationDepth = 0.1f * static_cast<float>((i % 5U) + 1U);
        }
    }
}

void assertDenseMacroAutomationsRestored(const core::state::CoreState& state) {
    using namespace core::state::macro;

    constexpr uint8_t kAutomationCount = 15;
    constexpr uint8_t kPointCount = 6;
    assert(state.pages.automation.entryCount == kAutomationCount);

    for (uint8_t i = 0; i < kAutomationCount; ++i) {
        const auto address = denseMacroAutomationAddress(i);
        const auto* slot = macroAutomationFindSlot(state.pages.automation, address);
        assert(slot != nullptr);
        assert(slot->automation.active);
        assert(slot->automation.pointCount == kPointCount);
        assert(std::fabs(
                   macroAutomationBeatsFromTicks(slot->automation.durationTicks) -
                   denseMacroAutomationDuration(i)
               ) < 0.0001f);
        assert(std::fabs(
                   macroAutomationBeatsFromTicks(slot->automation.windowOffsetTicks) -
                   denseMacroAutomationWindowOffset(i)
               ) < 0.0001f);

        for (uint8_t point = 0; point < kPointCount; ++point) {
            MacroCurvePoint restored{};
            assert(macroAutomationReadPoint(
                slot->automation,
                state.pages.automation.pointPool,
                point,
                false,
                restored
            ));
            assert(std::fabs(restored.value - denseMacroAutomationValue(i, point)) < 0.0001f);
        }

        if (denseMacroAutomationHasModulation(i)) {
            assert(slot->modulation.active);
            assert(slot->modulation.pointCount == 3);
            assert(std::fabs(
                       slot->modulationDepth -
                       (0.1f * static_cast<float>((i % 5U) + 1U))
                   ) < 0.0001f);
            for (uint8_t point = 0; point < 3; ++point) {
                MacroCurvePoint restored{};
                assert(macroAutomationReadPoint(
                    slot->modulation,
                    state.pages.automation.pointPool,
                    point,
                    true,
                    restored
                ));
                assert(
                    std::fabs(restored.value - denseMacroModulationValue(i, point)) <
                    0.0001f
                );
            }
        } else {
            assert(!slot->modulation.active);
        }
    }
}

void configureProjectGraphContent(core::state::sequencer::SequencerPatternState& pattern) {
    using namespace core::state::sequencer;

    const auto rootZero = rootStepNodeId(0);
    const auto rootFour = rootStepNodeId(4);

    const auto defaultMicro = createMicroSequence(pattern, rootZero, 2);
    assert(defaultMicro.ok);
    const auto* graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* defaultMicroSequence = graph->sequence(defaultMicro.id);
    assert(defaultMicroSequence != nullptr);
    const auto defaultMicroNode = static_cast<uint16_t>(defaultMicroSequence->firstStepNode + 1);
    assert(setNodeNoteOffset(pattern, defaultMicroNode, 12));
    assert(setNodeLocalVariationRange(pattern, defaultMicroNode, StepProperty::NOTE, 8));
    assert(setNodeChordMode(pattern, defaultMicroNode, StepSequencerChordMode::Single));

    const auto rootCycle = createCycleStateSet(pattern, rootZero, 3);
    assert(rootCycle.ok);
    graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* rootCycleSet = graph->cycleSet(rootCycle.id);
    assert(rootCycleSet != nullptr);
    const auto rootCycleNode = static_cast<uint16_t>(rootCycleSet->firstStateNode + 1);
    StepSequencerChordSpec rootCycleChord{};
    rootCycleChord.voiceCount = 4;
    rootCycleChord.color = 2;
    rootCycleChord.variant = 1;
    rootCycleChord.spread = 3;
    rootCycleChord.strum = -25;
    rootCycleChord.velocityCurve = 12;
    assert(setNodeChordSpec(pattern, rootCycleNode, rootCycleChord));
    assert(setNodeEnabledOverride(pattern, rootCycleNode, true));
    assert(setNodeNoteOffset(pattern, rootCycleNode, 7));
    assert(setNodeVelocityOffset(pattern, rootCycleNode, -20));
    assert(setNodeGateOffset(pattern, rootCycleNode, -15));
    assert(setNodeNudgeOffset(pattern, rootCycleNode, 10));
    assert(setNodeProbabilityOffset(pattern, rootCycleNode, -50));
    assert(setNodeLocalVariationRange(pattern, rootCycleNode, StepProperty::VELOCITY, 33));

    const auto stateMicro = createMicroSequence(pattern, rootCycleNode, 2);
    assert(stateMicro.ok);
    graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* stateMicroSequence = graph->sequence(stateMicro.id);
    assert(stateMicroSequence != nullptr);
    const auto stateMicroNode = static_cast<uint16_t>(stateMicroSequence->firstStepNode + 1);
    assert(setNodeNoteOffset(pattern, stateMicroNode, 5));
    assert(setNodeLocalVariationRange(pattern, stateMicroNode, StepProperty::GATE, 44));
    assert(setNodeChordMode(pattern, stateMicroNode, StepSequencerChordMode::Inherit));

    const auto stateCycle = createCycleStateSet(pattern, rootCycleNode, 5);
    assert(stateCycle.ok);
    graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* stateCycleSet = graph->cycleSet(stateCycle.id);
    assert(stateCycleSet != nullptr);
    const auto stateCycleNode = static_cast<uint16_t>(stateCycleSet->firstStateNode + 2);
    assert(setNodeNoteOffset(pattern, stateCycleNode, 3));
    assert(setNodeLocalVariationRange(pattern, stateCycleNode, StepProperty::NUDGE, 12));

    const auto rootMicro = createMicroSequence(pattern, rootFour, 2);
    assert(rootMicro.ok);
    graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* rootMicroSequence = graph->sequence(rootMicro.id);
    assert(rootMicroSequence != nullptr);
    const auto microNode = static_cast<uint16_t>(rootMicroSequence->firstStepNode + 1);
    StepSequencerChordSpec microChord{};
    microChord.voiceCount = 5;
    microChord.color = 3;
    microChord.variant = 2;
    microChord.spread = 4;
    microChord.strum = 18;
    microChord.velocityCurve = -9;
    assert(setNodeChordSpec(pattern, microNode, microChord));
    assert(setNodeNoteOffset(pattern, microNode, 5));
    assert(setNodeVelocityOffset(pattern, microNode, -12));
    assert(setNodeGateOffset(pattern, microNode, 25));
    assert(setNodeNudgeOffset(pattern, microNode, -8));
    assert(setNodeProbabilityOffset(pattern, microNode, -33));
    assert(setNodeLocalVariationRange(pattern, microNode, StepProperty::NOTE, 6));

    const auto nestedMicro = createMicroSequence(pattern, microNode, 2);
    assert(nestedMicro.ok);
    graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* nestedMicroSequence = graph->sequence(nestedMicro.id);
    assert(nestedMicroSequence != nullptr);
    const auto nestedMicroNode = static_cast<uint16_t>(nestedMicroSequence->firstStepNode + 1);
    assert(setNodeNoteOffset(pattern, nestedMicroNode, 9));
    assert(setNodeLocalVariationRange(pattern, nestedMicroNode, StepProperty::VELOCITY, 22));

    const auto nestedCycle = createCycleStateSet(pattern, microNode, 2);
    assert(nestedCycle.ok);
    graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* nestedCycleSet = graph->cycleSet(nestedCycle.id);
    assert(nestedCycleSet != nullptr);
    const auto nestedCycleNode = static_cast<uint16_t>(nestedCycleSet->firstStateNode + 1);
    assert(setNodeNoteOffset(pattern, nestedCycleNode, 4));
    assert(setNodeLocalVariationRange(pattern, nestedCycleNode, StepProperty::GATE, 18));
}

void assertProjectGraphContent(const core::state::sequencer::SequencerPatternState& pattern) {
    using namespace core::state::sequencer;

    const auto* graph = graphView(pattern);
    assert(graph != nullptr);

    const auto* rootZero = graph->stepNode(rootStepNodeId(0));
    assert(rootZero != nullptr);
    assert(rootZero->has(STEP_NODE_CHILD_SEQUENCE));
    assert(rootZero->has(STEP_NODE_CYCLE_SET));

    const auto* defaultMicroSequence = graph->sequence(rootZero->childSequenceId);
    assert(defaultMicroSequence != nullptr);
    assert(defaultMicroSequence->length == 2);
    const auto* defaultMicroNode = graph->stepNode(
        static_cast<uint16_t>(defaultMicroSequence->firstStepNode + 1)
    );
    assert(defaultMicroNode != nullptr);
    assert(defaultMicroNode->has(STEP_NODE_NOTE_OFFSET));
    assert(defaultMicroNode->has(STEP_NODE_CHORD_MODE));
    assert(defaultMicroNode->chordMode == StepSequencerChordMode::Single);
    assert(defaultMicroNode->noteOffset == 12);
    assert(nodeLocalVariationRange(*defaultMicroNode, StepProperty::NOTE) == 8);

    const auto* rootCycleSet = graph->cycleSet(rootZero->cycleSetId);
    assert(rootCycleSet != nullptr);
    assert(rootCycleSet->length == 3);
    const auto* rootCycleNode = graph->stepNode(
        static_cast<uint16_t>(rootCycleSet->firstStateNode + 1)
    );
    assert(rootCycleNode != nullptr);
    assert(rootCycleNode->has(STEP_NODE_ENABLED_OVERRIDE));
    assert(rootCycleNode->has(STEP_NODE_ENABLED_VALUE));
    assert(rootCycleNode->has(STEP_NODE_NOTE_OFFSET));
    assert(rootCycleNode->has(STEP_NODE_VELOCITY_OFFSET));
    assert(rootCycleNode->has(STEP_NODE_GATE_OFFSET));
    assert(rootCycleNode->has(STEP_NODE_NUDGE_OFFSET));
    assert(rootCycleNode->has(STEP_NODE_PROBABILITY_OFFSET));
    assert(rootCycleNode->has(STEP_NODE_CHILD_SEQUENCE));
    assert(rootCycleNode->has(STEP_NODE_CYCLE_SET));
    assert(rootCycleNode->has(STEP_NODE_CHORD_MODE));
    assert(rootCycleNode->has(STEP_NODE_CHORD_LOCAL));
    assert(rootCycleNode->chordMode == StepSequencerChordMode::Local);
    assert(rootCycleNode->chordSpec.voiceCount == 4);
    assert(rootCycleNode->chordSpec.color == 2);
    assert(rootCycleNode->chordSpec.variant == 1);
    assert(rootCycleNode->chordSpec.spread == 3);
    assert(rootCycleNode->chordSpec.strum == -25);
    assert(rootCycleNode->chordSpec.velocityCurve == 12);
    assert(rootCycleNode->noteOffset == 7);
    assert(rootCycleNode->velocityOffset == -20);
    assert(rootCycleNode->gateOffset == -15);
    assert(rootCycleNode->nudgeOffset == 10);
    assert(rootCycleNode->probabilityOffset == -50);
    assert(nodeLocalVariationRange(*rootCycleNode, StepProperty::VELOCITY) == 33);

    const auto* stateMicroSequence = graph->sequence(rootCycleNode->childSequenceId);
    assert(stateMicroSequence != nullptr);
    assert(stateMicroSequence->length == 2);
    const auto* stateMicroNode = graph->stepNode(
        static_cast<uint16_t>(stateMicroSequence->firstStepNode + 1)
    );
    assert(stateMicroNode != nullptr);
    assert(stateMicroNode->has(STEP_NODE_NOTE_OFFSET));
    assert(stateMicroNode->has(STEP_NODE_CHORD_MODE));
    assert(stateMicroNode->chordMode == StepSequencerChordMode::Inherit);
    assert(stateMicroNode->noteOffset == 5);
    assert(nodeLocalVariationRange(*stateMicroNode, StepProperty::GATE) == 44);

    const auto* stateCycleSet = graph->cycleSet(rootCycleNode->cycleSetId);
    assert(stateCycleSet != nullptr);
    assert(stateCycleSet->length == 5);
    const auto* stateCycleNode = graph->stepNode(
        static_cast<uint16_t>(stateCycleSet->firstStateNode + 2)
    );
    assert(stateCycleNode != nullptr);
    assert(stateCycleNode->has(STEP_NODE_NOTE_OFFSET));
    assert(stateCycleNode->noteOffset == 3);
    assert(nodeLocalVariationRange(*stateCycleNode, StepProperty::NUDGE) == 12);

    const auto* rootFour = graph->stepNode(rootStepNodeId(4));
    assert(rootFour != nullptr);
    assert(rootFour->has(STEP_NODE_CHILD_SEQUENCE));
    const auto* microSequence = graph->sequence(rootFour->childSequenceId);
    assert(microSequence != nullptr);
    assert(microSequence->length == 2);
    const auto* microNode = graph->stepNode(
        static_cast<uint16_t>(microSequence->firstStepNode + 1)
    );
    assert(microNode != nullptr);
    assert(microNode->has(STEP_NODE_NOTE_OFFSET));
    assert(microNode->has(STEP_NODE_VELOCITY_OFFSET));
    assert(microNode->has(STEP_NODE_GATE_OFFSET));
    assert(microNode->has(STEP_NODE_NUDGE_OFFSET));
    assert(microNode->has(STEP_NODE_PROBABILITY_OFFSET));
    assert(microNode->has(STEP_NODE_CHORD_MODE));
    assert(microNode->has(STEP_NODE_CHORD_LOCAL));
    assert(microNode->chordMode == StepSequencerChordMode::Local);
    assert(microNode->chordSpec.voiceCount == 5);
    assert(microNode->chordSpec.color == 3);
    assert(microNode->chordSpec.variant == 2);
    assert(microNode->chordSpec.spread == 4);
    assert(microNode->chordSpec.strum == 18);
    assert(microNode->chordSpec.velocityCurve == -9);
    assert(microNode->noteOffset == 5);
    assert(microNode->velocityOffset == -12);
    assert(microNode->gateOffset == 25);
    assert(microNode->nudgeOffset == -8);
    assert(microNode->probabilityOffset == -33);
    assert(nodeLocalVariationRange(*microNode, StepProperty::NOTE) == 6);

    assert(microNode->has(STEP_NODE_CHILD_SEQUENCE));
    assert(microNode->has(STEP_NODE_CYCLE_SET));
    const auto* nestedMicroSequence = graph->sequence(microNode->childSequenceId);
    assert(nestedMicroSequence != nullptr);
    assert(nestedMicroSequence->length == 2);
    const auto* nestedMicroNode = graph->stepNode(
        static_cast<uint16_t>(nestedMicroSequence->firstStepNode + 1)
    );
    assert(nestedMicroNode != nullptr);
    assert(nestedMicroNode->has(STEP_NODE_NOTE_OFFSET));
    assert(nestedMicroNode->noteOffset == 9);
    assert(nodeLocalVariationRange(*nestedMicroNode, StepProperty::VELOCITY) == 22);

    const auto* nestedCycleSet = graph->cycleSet(microNode->cycleSetId);
    assert(nestedCycleSet != nullptr);
    assert(nestedCycleSet->length == 2);
    const auto* nestedCycleNode = graph->stepNode(
        static_cast<uint16_t>(nestedCycleSet->firstStateNode + 1)
    );
    assert(nestedCycleNode != nullptr);
    assert(nestedCycleNode->has(STEP_NODE_NOTE_OFFSET));
    assert(nestedCycleNode->noteOffset == 4);
    assert(nodeLocalVariationRange(*nestedCycleNode, StepProperty::GATE) == 18);
}

void configureProjectSession(core::state::CoreState& state) {
    std::strncpy(
        state.project.metadata.id.data(),
        "p777",
        state.project.metadata.id.size() - 1
    );
    std::strncpy(
        state.project.metadata.name.data(),
        "p777",
        state.project.metadata.name.size() - 1
    );
    state.project.metadata.modifiedCounter = 77;
    state.project.metadata.dirty = true;
    state.project.metadata.hasSavedIdentity = true;

    state.statusBar.tempo.set(128.5f);
    state.statusBar.tempoDisplay.set(128.5f);
    state.projectNavigation.transportSwingPercent = 18;
    state.projectNavigation.transportRunMode = 1;
    state.projectNavigation.patternsInheritScale = false;
    state.projectNavigation.clipsInheritScale = true;
    state.projectNavigation.stepPasteMode =
        core::state::project::ProjectStepPasteMode::WRAP;

    StepSequencerScaleSettings scale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    assert(state.sequencerTracks.setProjectScaleSettings(scale));

    assert(state.setSharedTrackState(0x0007, 2));
    auto& page = state.pages.activePageData();
    std::strncpy(page.name, "FullSnap", sizeof(page.name) - 1);
    page.cc[2] = 91;
    page.values[2] = 0.625f;
    auto* macroSlot = core::state::macro::macroAutomationGetOrCreateSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 2,
        }
    );
    assert(macroSlot != nullptr);
    core::state::macro::MacroAutomationLane automation;
    automation.durationBeats = 2.0f;
    assert(core::state::macro::macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(core::state::macro::macroAutomationAppendPoint(automation, 1.0f, 0.8f));
    assert(core::state::macro::macroAutomationAssignAutomation(
        state.pages.automation,
        *macroSlot,
        automation
    ));
    core::state::macro::MacroModulationShape modulation;
    modulation.durationBeats = 2.0f;
    assert(core::state::macro::macroModulationAppendPoint(modulation, 0.0f, -0.25f));
    assert(core::state::macro::macroModulationAppendPoint(modulation, 1.0f, 0.25f));
    assert(core::state::macro::macroAutomationAssignModulation(
        state.pages.automation,
        *macroSlot,
        modulation
    ));
    macroSlot->modulationDepth = 0.5f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.length.set(12);
    state.sequencer.pattern.stepsPerBeat.set(5);
    state.sequencer.pattern.midiChannel.set(6);
    state.sequencer.setStepDataAt(0, 64, 100, 80);
    state.sequencer.setStepDataAt(4, 67, 88, 55);
    state.sequencer.pattern.toggle(0);
    state.sequencer.pattern.toggle(4);
    state.sequencer.focusedStep.set(4);
    state.sequencer.page.set(0);
    configureProjectGraphContent(state.sequencer.pattern);
}

void assertRuntimeMatchesConfigured(core::state::CoreState& state) {
    assert(std::strcmp(state.project.metadata.id.data(), "p777") == 0);
    assert(std::strcmp(state.project.metadata.name.data(), "p777") == 0);
    assert(state.project.metadata.modifiedCounter == 77);
    assert(state.project.metadata.dirty);
    assert(state.project.metadata.hasSavedIdentity);
    assert(state.statusBar.tempo.get() == 128.5f);
    assert(state.projectNavigation.transportSwingPercent == 18);
    assert(state.projectNavigation.transportRunMode == 1);
    assert(!state.projectNavigation.patternsInheritScale);
    assert(state.projectNavigation.clipsInheritScale);
    assert(
        state.projectNavigation.stepPasteMode ==
        core::state::project::ProjectStepPasteMode::WRAP
    );

    StepSequencerScaleSettings expectedScale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    assert(sameScale(state.sequencerTracks.projectScaleSettings(), expectedScale));

    assert(state.sharedTrackEnabledMask.get() == 0x0007);
    assert(state.sharedTrackActive.get() == 2);
    assert(std::strcmp(state.pages.activePageData().name, "FullSnap") == 0);
    assert(state.pages.activePageData().cc[2] == 91);
    assert(state.pages.activePageData().values[2] == 0.625f);
    assert(state.macros.slots[2].value.get() == 0.625f);
    const auto* macroSlot = core::state::macro::macroAutomationFindSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 2,
        }
    );
    assert(macroSlot != nullptr);
    assert(macroSlot->automation.active);
    assert(macroSlot->automation.pointCount == 2);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
               macroSlot->automation.durationTicks
           ) == 2.0f);
    core::state::macro::MacroCurvePoint firstAutomationPoint{};
    core::state::macro::MacroCurvePoint secondAutomationPoint{};
    assert(core::state::macro::macroAutomationReadPoint(
        macroSlot->automation,
        state.pages.automation.pointPool,
        0,
        false,
        firstAutomationPoint
    ));
    assert(core::state::macro::macroAutomationReadPoint(
        macroSlot->automation,
        state.pages.automation.pointPool,
        1,
        false,
        secondAutomationPoint
    ));
    assert(std::fabs(firstAutomationPoint.value - 0.2f) < 0.0001f);
    assert(std::fabs(secondAutomationPoint.value - 0.8f) < 0.0001f);
    assert(macroSlot->modulation.active);
    assert(macroSlot->modulation.pointCount == 2);
    assert(macroSlot->modulationDepth == 0.5f);

    assert(state.sequencer.pattern.length.get() == 12);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 5);
    assert(state.sequencer.pattern.midiChannel.get() == 6);
    assert(state.sequencer.pattern.isEnabled(0));
    assert(state.sequencer.pattern.isEnabled(4));
    assert(state.sequencer.pattern.note[0] == 64);
    assert(state.sequencer.pattern.note[4] == 67);
    assert(state.sequencer.focusedStep.get() == 4);
    assertProjectGraphContent(state.sequencer.pattern);
}

void test_project_snapshot_roundtrip_restores_runtime_state() {
    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    configureProjectSession(sourceState);

    project::ProjectSnapshot sourceSnapshot;
    assert(project::captureProjectSnapshot(sourceState, sourceSnapshot));

    auto buffer = core::app::makeExtmemUnique<std::array<uint8_t, kProjectSnapshotScratchSize>>();
    assert(buffer);
    auto encodeResult = snapshot_codec::encodeProjectSnapshot(
        sourceSnapshot,
        buffer->data(),
        static_cast<uint32_t>(buffer->size())
    );
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot loadedSnapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        buffer->data(),
        encodeResult.bytesWritten,
        loadedSnapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.loadStatus == project_file::LoadStatus::OK);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(core::state::sequencer::sameMusicalHistorySnapshot(
        sourceSnapshot.sequencer,
        loadedSnapshot.sequencer
    ));

    test_support::CoreStorages targetStorages;
    auto targetState = makeCoreState(targetStorages);
    assert(project::applyProjectSnapshot(targetState, loadedSnapshot));
    assertRuntimeMatchesConfigured(targetState);

    std::cout << "[PASS] test_project_snapshot_roundtrip_restores_runtime_state\n";
}

void test_project_snapshot_roundtrip_preserves_dense_macro_automation_pool() {
    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    configureDenseMacroAutomations(sourceState);

    project::ProjectSnapshot sourceSnapshot;
    assert(project::captureProjectSnapshot(sourceState, sourceSnapshot));
    assert(sourceSnapshot.macroAutomation);
    const uint8_t expectedAutomationEntries = sourceSnapshot.macroAutomation->entryCount;
    const uint16_t expectedAutomationPoints = sourceSnapshot.macroAutomation->pointPool.used;
    assert(expectedAutomationEntries == 15);
    assert(expectedAutomationPoints > expectedAutomationEntries);

    auto buffer = core::app::makeExtmemUnique<std::array<uint8_t, kProjectSnapshotScratchSize>>();
    assert(buffer);
    auto encodeResult = snapshot_codec::encodeProjectSnapshot(
        sourceSnapshot,
        buffer->data(),
        static_cast<uint32_t>(buffer->size())
    );
    assert(encodeResult.status == project_file::Status::OK);

    project_file::DecodedChunkView encodedChunks[project_file::MAX_CHUNKS] = {};
    project_file::LoadReport containerReport{};
    const auto containerDecode = project_file::decode(
        buffer->data(),
        encodeResult.bytesWritten,
        encodedChunks,
        project_file::MAX_CHUNKS,
        &containerReport
    );
    assert(containerDecode.status == project_file::Status::OK);
    const project_file::DecodedChunkView* automationChunk = nullptr;
    for (uint16_t i = 0; i < containerDecode.chunkCount; ++i) {
        if (encodedChunks[i].id ==
            project_file::chunkIdValue(project_file::ChunkId::MACRO_AUTOMATION)) {
            automationChunk = &encodedChunks[i];
            break;
        }
    }
    assert(automationChunk != nullptr);
    assert(
        automationChunk->versionMinor ==
        snapshot_codec::PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR
    );
    assert(automationChunk->size ==
           snapshot_codec::PROJECT_MACRO_AUTOMATION_HEADER_SIZE +
               static_cast<uint32_t>(expectedAutomationEntries) *
                   snapshot_codec::PROJECT_MACRO_AUTOMATION_ENTRY_SIZE +
               static_cast<uint32_t>(expectedAutomationPoints) *
                   snapshot_codec::PROJECT_MACRO_AUTOMATION_POINT_SIZE);
    assert(automationChunk->size < sizeof(core::state::macro::MacroAutomationBankState));

    project::ProjectSnapshot loadedSnapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        buffer->data(),
        encodeResult.bytesWritten,
        loadedSnapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.loadStatus == project_file::LoadStatus::OK);
    assert(report.ok());

    test_support::CoreStorages targetStorages;
    auto targetState = makeCoreState(targetStorages);
    assert(project::applyProjectSnapshot(targetState, loadedSnapshot));
    assertDenseMacroAutomationsRestored(targetState);

    std::cout << "[PASS] test_project_snapshot_roundtrip_preserves_dense_macro_automation_pool\n";
}

void test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks() {
    project::ProjectState state;
    std::strncpy(state.metadata.id.data(), "p010", state.metadata.id.size() - 1);
    std::strncpy(state.metadata.name.data(), "p010", state.metadata.name.size() - 1);

    uint8_t bytes[512] = {};
    auto encodeResult = core::persistence::project_state_codec::encodeProjectState(
        state,
        bytes,
        sizeof(bytes)
    );
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot snapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        bytes,
        encodeResult.bytesWritten,
        snapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(reportHas(report, project_file::LoadCode::MISSING_OPTIONAL_CHUNK));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));
    assert(std::strcmp(snapshot.project.metadata.id.data(), "p010") == 0);
    assert(std::strcmp(snapshot.project.metadata.name.data(), "p010") == 0);
    assert(snapshot.sharedTrackEnabledMask == core::state::macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK);
    assert(snapshot.macroAutomation);
    assert(snapshot.macroAutomation->entryCount == 0);

    std::cout << "[PASS] test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks\n";
}

void test_project_snapshot_future_sequencer_chunk_blocks_overwrite() {
    const uint8_t payload[] = {1, 2, 3};
    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
        .versionMajor = static_cast<uint8_t>(
            snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR + 1
        ),
        .versionMinor = 0,
        .flags = 0,
        .data = payload,
        .size = sizeof(payload),
    }};

    uint8_t bytes[160] = {};
    auto encodeResult = project_file::encode(chunks, 1, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot snapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        bytes,
        encodeResult.bytesWritten,
        snapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::PARTIAL);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    std::cout << "[PASS] test_project_snapshot_future_sequencer_chunk_blocks_overwrite\n";
}

void test_project_snapshot_stale_macro_state_chunk_defaults_and_blocks_overwrite() {
    using MacroStatePayload =
        std::array<uint8_t, snapshot_codec::PROJECT_MACRO_STATE_PAYLOAD_SIZE>;

    auto macroPayload = core::app::makeExtmemUnique<MacroStatePayload>();
    assert(macroPayload);

    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::MACRO_STATE),
        .versionMajor = snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
        .versionMinor = static_cast<uint8_t>(
            snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR - 1U
        ),
        .flags = 0,
        .data = macroPayload->data(),
        .size = static_cast<uint32_t>(macroPayload->size()),
    }};

    auto buffer =
        core::app::makeExtmemUnique<std::array<uint8_t, kProjectSnapshotScratchSize>>();
    assert(buffer);
    auto encodeResult = project_file::encode(chunks, 1, 0, buffer->data(), buffer->size());
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot snapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        buffer->data(),
        encodeResult.bytesWritten,
        snapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!decodeResult.overwriteSafe);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    std::cout << "[PASS] test_project_snapshot_stale_macro_state_chunk_defaults_and_blocks_overwrite\n";
}

void test_project_snapshot_stale_sequencer_chunk_defaults_and_blocks_overwrite() {
    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    configureProjectSession(sourceState);

    auto envelope = core::app::makeExtmemUnique<core::persistence::sequencer_codec::EnvelopeBuffer>();
    assert(envelope);
    const auto encodedSequencer =
        core::persistence::sequencer_codec::fillProjectSequencerEnvelope(
            sourceState.sequencerTracks,
            sourceState.sequencer,
            envelope->bytes.data(),
            static_cast<uint16_t>(envelope->bytes.size())
        );
    assert(encodedSequencer.ok);

    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
        .versionMajor = snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
        .versionMinor = static_cast<uint8_t>(
            snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR - 1U
        ),
        .flags = 0,
        .data = envelope->bytes.data(),
        .size = encodedSequencer.size,
    }};

    auto buffer = core::app::makeExtmemUnique<std::array<uint8_t, kProjectSnapshotScratchSize>>();
    assert(buffer);
    auto encodeResult = project_file::encode(chunks, 1, 0, buffer->data(), buffer->size());
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectSnapshot snapshot;
    project_file::LoadReport report{};
    auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        buffer->data(),
        encodeResult.bytesWritten,
        snapshot,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!decodeResult.overwriteSafe);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    test_support::CoreStorages targetStorages;
    auto targetState = makeCoreState(targetStorages);
    assert(project::applyProjectSnapshot(targetState, snapshot));
    assert(targetState.sequencer.pattern.length.get() ==
           core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(targetState.sequencer.pattern.length.get() != sourceState.sequencer.pattern.length.get());
    assert(!targetState.sequencer.pattern.isEnabled(0));

    std::cout << "[PASS] test_project_snapshot_stale_sequencer_chunk_defaults_and_blocks_overwrite\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectSnapshotPersistenceCodec tests\n";
    std::cout << "==============================================\n\n";

    test_project_snapshot_roundtrip_restores_runtime_state();
    test_project_snapshot_roundtrip_preserves_dense_macro_automation_pool();
    test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks();
    test_project_snapshot_future_sequencer_chunk_blocks_overwrite();
    test_project_snapshot_stale_macro_state_chunk_defaults_and_blocks_overwrite();
    test_project_snapshot_stale_sequencer_chunk_defaults_and_blocks_overwrite();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
