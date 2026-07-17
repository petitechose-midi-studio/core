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
#include "support/ProjectStatePersistenceTestSupport.hpp"
#include "../../src/persistence/SequencerPersistenceEnvelope.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"
#include "../../src/state/sequencer/SequencerScaleState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/ProjectControlTestUtils.hpp"
#include "../support/ProjectSequencerEnvelopeTestSupport.hpp"

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

core::state::macro::MacroCurvePlaybackState denseAutomationPlaybackState(uint8_t index) {
    return (index % 3U) == 0U
        ? core::state::macro::MacroCurvePlaybackState::OFF
        : core::state::macro::MacroCurvePlaybackState::ACTIVE;
}

core::state::macro::MacroCurvePlaybackState denseModulationPlaybackState(uint8_t index) {
    return (index % 8U) == 4U
        ? core::state::macro::MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD
        : core::state::macro::MacroCurvePlaybackState::ACTIVE;
}

core::state::macro::MacroCurvePlaybackState expectedDenseModulationPlaybackState(
    uint8_t index
) {
    return denseModulationPlaybackState(index) ==
               core::state::macro::MacroCurvePlaybackState::ACTIVE
        ? core::state::macro::MacroCurvePlaybackState::ACTIVE
        : core::state::macro::MacroCurvePlaybackState::OFF;
}

core::state::macro::MacroModulationOrigin denseModulationOrigin(uint8_t index) {
    return (index % 8U) == 0U
        ? core::state::macro::MacroModulationOrigin::CONVERTED_MEAN
        : core::state::macro::MacroModulationOrigin::CONVERTED_MIN;
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
        assert(test_support::project_control::assignAutomation(
            state.pages.control,
            address,
            lane
        ));
        if (denseAutomationPlaybackState(i) != MacroCurvePlaybackState::ACTIVE) {
            assert(core::state::modulation::setProjectControlAutomationEnabled(
                state.pages.control,
                address,
                false
            ));
        }
        const float windowOffset = denseMacroAutomationWindowOffset(i);
        assert(core::state::modulation::setProjectControlAutomationWindowOffsetBeats(
            state.pages.control,
            address,
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
            assert(test_support::project_control::assignModulation(
                state.pages.control,
                address,
                modulation,
                0.1f * static_cast<float>((i % 5U) + 1U)
            ));
            auto view = test_support::project_control::readSlot(
                state.pages.control,
                address
            );
            auto* curve = test_support::project_control::mutableCurve(
                state.pages.control,
                view.modulationCurveId
            );
            assert(curve != nullptr);
            curve->origin = denseModulationOrigin(i) ==
                    MacroModulationOrigin::CONVERTED_MEAN
                ? core::state::modulation::ProjectCurveOrigin::CONVERTED_MEAN
                : core::state::modulation::ProjectCurveOrigin::CONVERTED_MIN;
            if (denseModulationPlaybackState(i) !=
                MacroCurvePlaybackState::ACTIVE) {
                assert(core::state::modulation::setProjectControlModulationEnabled(
                    state.pages.control,
                    address,
                    false
                ));
            }
        }
    }
}

void assertDenseMacroAutomationsRestored(const core::state::CoreState& state) {
    using namespace core::state::macro;

    constexpr uint8_t kAutomationCount = 15;
    constexpr uint8_t kPointCount = 6;
    assert(state.pages.control.authored.automation.entryCount == kAutomationCount);

    for (uint8_t i = 0; i < kAutomationCount; ++i) {
        const auto address = denseMacroAutomationAddress(i);
        const auto slot = test_support::project_control::readSlot(
            state.pages.control,
            address
        );
        assert(slot.automationStored);
        assert(slot.automationEnabled ==
               (denseAutomationPlaybackState(i) == MacroCurvePlaybackState::ACTIVE));
        assert(slot.compatibility.automation.pointCount == kPointCount);
        assert(std::fabs(
                   macroAutomationBeatsFromTicks(slot.compatibility.automation.durationTicks) -
                   denseMacroAutomationDuration(i)
               ) < 0.0001f);
        assert(std::fabs(
                   macroAutomationBeatsFromTicks(slot.compatibility.automation.windowOffsetTicks) -
                   denseMacroAutomationWindowOffset(i)
               ) < 0.0001f);

        for (uint8_t point = 0; point < kPointCount; ++point) {
            const auto restored = test_support::project_control::readCurvePoint(
                state.pages.control,
                slot.automationCurveId,
                point,
                false
            );
            assert(std::fabs(restored.value - denseMacroAutomationValue(i, point)) < 0.0001f);
        }

        if (denseMacroAutomationHasModulation(i)) {
            assert(slot.modulationStored);
            assert(slot.compatibility.modulation.playbackState ==
                   expectedDenseModulationPlaybackState(i));
            assert(slot.compatibility.modulation.modulationOrigin == denseModulationOrigin(i));
            assert(slot.compatibility.modulation.pointCount == 3);
            assert(std::fabs(
                       slot.compatibility.modulationDepth -
                       (0.1f * static_cast<float>((i % 5U) + 1U))
                   ) < 0.0001f);
            for (uint8_t point = 0; point < 3; ++point) {
                const auto restored = test_support::project_control::readCurvePoint(
                    state.pages.control,
                    slot.modulationCurveId,
                    point,
                    true
                );
                assert(
                    std::fabs(restored.value - denseMacroModulationValue(i, point)) <
                    0.0001f
                );
            }
        } else {
            assert(!slot.modulationStored);
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
    const auto macroAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 2,
    };
    core::state::macro::MacroAutomationLane automation;
    automation.durationBeats = 2.0f;
    assert(core::state::macro::macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(core::state::macro::macroAutomationAppendPoint(automation, 1.0f, 0.8f));
    assert(test_support::project_control::assignAutomation(
        state.pages.control,
        macroAddress,
        automation
    ));
    core::state::macro::MacroModulationShape modulation;
    modulation.durationBeats = 2.0f;
    assert(core::state::macro::macroModulationAppendPoint(modulation, 0.0f, -0.25f));
    assert(core::state::macro::macroModulationAppendPoint(modulation, 1.0f, 0.25f));
    assert(test_support::project_control::assignModulation(
        state.pages.control,
        macroAddress,
        modulation,
        0.5f
    ));
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
    state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::GATE);
    configureProjectGraphContent(state.sequencer.pattern);
}

oc::note::sequencer::StepSequencerGraph makeMaxDensityProjectGraph() {
    using namespace oc::note::sequencer;

    StepSequencerGraph graph;
    graph.reset();
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.stepNodeCount = StepSequencerGraphLimits::MAX_STEP_NODES;
    graph.sequenceCount = StepSequencerGraphLimits::MAX_SEQUENCES;
    graph.cycleSetCount = StepSequencerGraphLimits::MAX_CYCLE_SETS;

    graph.sequences[0].kind = StepSequencerSequenceKind::RootPattern;
    graph.sequences[0].firstStepNode = 0;
    graph.sequences[0].length =
        core::state::sequencer::SequencerPatternState::MAX_STEPS;
    for (uint16_t i = 1; i < graph.sequenceCount; ++i) {
        graph.sequences[i].kind = StepSequencerSequenceKind::MicroSequence;
        graph.sequences[i].firstStepNode = 0;
        graph.sequences[i].length = 1;
    }
    for (uint16_t i = 0; i < graph.cycleSetCount; ++i) {
        graph.cycleSets[i].firstStateNode = 0;
        graph.cycleSets[i].length = 1;
    }
    for (uint16_t i = 0; i < graph.stepNodeCount; ++i) {
        graph.stepNodes[i].flags = STEP_NODE_NOTE_OFFSET;
        graph.stepNodes[i].noteOffset = static_cast<int8_t>(i % 12U);
    }
    return graph;
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
    const auto macroSlot = test_support::project_control::readSlot(
        state.pages.control,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 2,
        }
    );
    assert(macroSlot.automationEnabled);
    assert(macroSlot.compatibility.automation.pointCount == 2);
    assert(core::state::macro::macroAutomationBeatsFromTicks(
               macroSlot.compatibility.automation.durationTicks
           ) == 2.0f);
    const auto firstAutomationPoint = test_support::project_control::readCurvePoint(
        state.pages.control,
        macroSlot.automationCurveId,
        0,
        false
    );
    const auto secondAutomationPoint = test_support::project_control::readCurvePoint(
        state.pages.control,
        macroSlot.automationCurveId,
        1,
        false
    );
    assert(std::fabs(firstAutomationPoint.value - 0.2f) < 0.0001f);
    assert(std::fabs(secondAutomationPoint.value - 0.8f) < 0.0001f);
    assert(macroSlot.modulationEnabled);
    assert(macroSlot.compatibility.modulation.pointCount == 2);
    assert(std::fabs(macroSlot.compatibility.modulationDepth - 0.5f) < 0.0001f);

    assert(state.sequencer.pattern.length.get() == 12);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 5);
    assert(state.sequencer.pattern.midiChannel.get() == 6);
    assert(state.sequencer.pattern.isEnabled(0));
    assert(state.sequencer.pattern.isEnabled(4));
    assert(state.sequencer.pattern.note[0] == 64);
    assert(state.sequencer.pattern.note[4] == 67);
    assert(state.sequencer.focusedStep.get() == 4);
    assert(state.sequencer.activeStepProperty.get() ==
           core::state::sequencer::StepProperty::GATE);
    assertProjectGraphContent(state.sequencer.pattern);
}

void test_project_sequencer_snapshot_encoder_is_deterministic() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));

    auto firstEnvelope = core::app::makeExtmemUnique<
        core::persistence::sequencer_codec::EnvelopeBuffer>();
    auto secondEnvelope = core::app::makeExtmemUnique<
        core::persistence::sequencer_codec::EnvelopeBuffer>();
    assert(firstEnvelope && secondEnvelope);

    const auto firstEncoded =
        test_support::encodeProjectSequencerSnapshot(snapshot.sequencer, *firstEnvelope);
    const auto secondEncoded =
        test_support::encodeProjectSequencerSnapshot(snapshot.sequencer, *secondEnvelope);
    assert(firstEncoded.ok && secondEncoded.ok);
    assert(firstEncoded.size == secondEncoded.size);
    assert(std::memcmp(
        firstEnvelope->bytes.data(),
        secondEnvelope->bytes.data(),
        firstEncoded.size
    ) == 0);

    std::cout << "[PASS] test_project_sequencer_snapshot_encoder_is_deterministic\n";
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
    const uint32_t beforeRuntimeOwnerRevision =
        targetState.macroRuntimeOwnerRevision.get();
    const auto manualAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = targetState.pages.currentActiveTrack(),
        .page = targetState.pages.currentActivePage(),
        .macro = 0,
    };
    assert(targetState.macroUi.manualOverrides.activate(manualAddress, 0.73f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(project::applyProjectSnapshot(targetState, loadedSnapshot));
    assert(targetState.macroRuntimeOwnerRevision.get() == beforeRuntimeOwnerRevision + 1U);
    assert(!targetState.macroUi.manualOverrides.activeFor(manualAddress));
    assertRuntimeMatchesConfigured(targetState);

    std::cout << "[PASS] test_project_snapshot_roundtrip_restores_runtime_state\n";
}

void test_project_snapshot_roundtrips_sequencer_chunk_larger_than_u16() {
    using namespace core::state::sequencer;

    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    const auto graph = makeMaxDensityProjectGraph();
    const uint8_t activeTrack = sourceState.sequencerTracks.activeTrackIndex();
    for (uint8_t track = 0; track < sourceState.sequencerTracks.TRACK_COUNT; ++track) {
        auto& pattern = track == activeTrack
            ? sourceState.sequencer.pattern
            : sourceState.sequencerTracks.track(track);
        assert(copyGraph(pattern, &graph, static_cast<uint32_t>(track) + 1U));
    }

    project::ProjectSnapshot sourceSnapshot;
    assert(project::captureProjectSnapshot(sourceState, sourceSnapshot));
    auto envelope = core::app::makeExtmemUnique<
        core::persistence::sequencer_codec::EnvelopeBuffer>();
    assert(envelope);
    const auto encodedEnvelope =
        test_support::encodeProjectSequencerSnapshot(sourceSnapshot.sequencer, *envelope);
    assert(encodedEnvelope.ok);
    assert(encodedEnvelope.size > UINT16_MAX);

    auto projectBytes = core::app::makeExtmemUnique<
        std::array<uint8_t, kProjectSnapshotScratchSize>>();
    assert(projectBytes);
    const auto encodedProject = snapshot_codec::encodeProjectSnapshot(
        sourceSnapshot,
        projectBytes->data(),
        static_cast<uint32_t>(projectBytes->size())
    );
    assert(encodedProject.status == project_file::Status::OK);

    project::ProjectSnapshot loadedSnapshot;
    project_file::LoadReport report{};
    const auto decodedProject = snapshot_codec::decodeProjectSnapshot(
        projectBytes->data(),
        encodedProject.bytesWritten,
        loadedSnapshot,
        &report
    );
    assert(decodedProject.ok);
    assert(decodedProject.loadStatus == project_file::LoadStatus::OK);
    assert(decodedProject.overwriteSafe);
    assert(report.ok());
    assert(sameMusicalHistorySnapshot(
        sourceSnapshot.sequencer,
        loadedSnapshot.sequencer
    ));

    std::cout
        << "[PASS] test_project_snapshot_roundtrips_sequencer_chunk_larger_than_u16\n";
}

void test_project_snapshot_roundtrip_preserves_dense_macro_automation_pool() {
    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    configureDenseMacroAutomations(sourceState);

    project::ProjectSnapshot sourceSnapshot;
    assert(project::captureProjectSnapshot(sourceState, sourceSnapshot));
    assert(sourceSnapshot.projectControl);
    const uint16_t expectedAutomationEntries =
        sourceSnapshot.projectControl->automation.entryCount;
    const uint16_t expectedCurvePoints =
        sourceSnapshot.projectControl->curves.pointCount;
    assert(expectedAutomationEntries == 15);
    assert(expectedCurvePoints > expectedAutomationEntries);
    assert(sourceSnapshot.projectControl->modulation.sourceCount > 0U);

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
    const project_file::DecodedChunkView* modulationChunk = nullptr;
    for (uint16_t i = 0; i < containerDecode.chunkCount; ++i) {
        if (encodedChunks[i].id ==
            project_file::chunkIdValue(project_file::ChunkId::MACRO_AUTOMATION)) {
            automationChunk = &encodedChunks[i];
        } else if (encodedChunks[i].id ==
                   project_file::chunkIdValue(
                       project_file::ChunkId::MODULATION_GRAPH
                   )) {
            modulationChunk = &encodedChunks[i];
        }
    }
    assert(automationChunk != nullptr);
    assert(modulationChunk != nullptr);
    assert(
        automationChunk->versionMinor ==
        snapshot_codec::PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR
    );
    assert(
        modulationChunk->versionMinor ==
        snapshot_codec::PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR
    );
    assert(automationChunk->size >
           core::persistence::project_control_codec::
               PROJECT_CONTROL_CHUNK_HEADER_SIZE);
    assert(modulationChunk->size >
           core::persistence::project_control_codec::
               PROJECT_CONTROL_CHUNK_HEADER_SIZE);
    assert(automationChunk->size + modulationChunk->size <=
           snapshot_codec::PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE);

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

void test_project_snapshot_rejects_invalid_project_control_references() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);
    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    assert(snapshot.projectControl);
    assert(snapshot.projectControl->automation.entryCount > 0U);
    assert(snapshot.projectControl->modulation.outputBindingCount > 0U);
    auto buffer = core::app::makeExtmemUnique<
        std::array<uint8_t, kProjectSnapshotScratchSize>>();
    assert(buffer);

    const auto validCurveId =
        snapshot.projectControl->automation.entries[0].curveId;
    snapshot.projectControl->automation.entries[0].curveId =
        core::state::modulation::ProjectCurveId{UINT32_MAX};
    auto result = snapshot_codec::encodeProjectSnapshot(
        snapshot,
        buffer->data(),
        static_cast<uint32_t>(buffer->size())
    );
    assert(result.status == project_file::Status::INVALID_ARGUMENT);
    assert(result.bytesWritten == 0);

    snapshot.projectControl->automation.entries[0].curveId = validCurveId;
    const auto validSourceId =
        snapshot.projectControl->modulation.outputBindings[0].sourceId;
    snapshot.projectControl->modulation.outputBindings[0].sourceId =
        core::state::modulation::ModulatorId{UINT32_MAX};
    result = snapshot_codec::encodeProjectSnapshot(
        snapshot,
        buffer->data(),
        static_cast<uint32_t>(buffer->size())
    );
    assert(result.status == project_file::Status::INVALID_ARGUMENT);
    assert(result.bytesWritten == 0);
    snapshot.projectControl->modulation.outputBindings[0].sourceId =
        validSourceId;

    std::cout
        << "[PASS] Project snapshot rejects dangling Project Control references\n";
}

void test_project_snapshot_migrates_macro_automation_v14_lifecycle_defaults() {
    using namespace core::state::macro;

    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    configureProjectSession(sourceState);
    const MacroAutomationSlotAddress address{
        .track = sourceState.pages.currentActiveTrack(),
        .page = sourceState.pages.currentActivePage(),
        .macro = 2,
    };

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(sourceState, snapshot));
    auto encoded = core::app::makeExtmemUnique<
        std::array<uint8_t, kProjectSnapshotScratchSize>>();
    auto legacyEncoded = core::app::makeExtmemUnique<
        std::array<uint8_t, kProjectSnapshotScratchSize>>();
    auto legacyMacroPayload = core::app::makeExtmemUnique<
        std::array<uint8_t, snapshot_codec::PROJECT_MACRO_AUTOMATION_MAX_PAYLOAD_SIZE>>();
    auto legacyBank = core::app::makeExtmemUnique<MacroAutomationBankState>();
    assert(encoded && legacyEncoded && legacyMacroPayload && legacyBank);

    uint16_t automationPointCount = 0;
    uint16_t modulationPointCount = 0;
    MacroAutomationSlotState legacySlot{};
    assert(core::state::modulation::captureProjectControlMacroSlot(
        sourceState.pages.control,
        address,
        legacySlot,
        legacyBank->pointPool.points.data(),
        MACRO_AUTOMATION_POINT_POOL_CAPACITY,
        automationPointCount,
        modulationPointCount
    ));
    legacySlot.automation.playbackState = MacroCurvePlaybackState::OFF;
    legacySlot.modulation.playbackState =
        MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD;
    legacySlot.modulation.modulationOrigin = MacroModulationOrigin::CONVERTED_FIRST;
    legacyBank->entryCount = 1;
    legacyBank->entries[0] = {
        .active = true,
        .address = address,
        .state = legacySlot,
    };
    legacyBank->pointPool.used = static_cast<uint16_t>(
        automationPointCount + modulationPointCount
    );
    uint32_t legacyMacroSize = 0;
    assert(core::persistence::macro_automation_legacy_codec::encodeV15(
        *legacyBank,
        legacyMacroPayload->data(),
        static_cast<uint32_t>(legacyMacroPayload->size()),
        legacyMacroSize
    ));

    constexpr uint32_t kEntryAddressBytes = 4;
    constexpr uint32_t kCurvePlaybackOffset = 1;
    constexpr uint32_t kCurveOriginOffset =
        snapshot_codec::PROJECT_MACRO_AUTOMATION_CURVE_REF_SIZE - 1U;
    const uint32_t entryOffset =
        snapshot_codec::PROJECT_MACRO_AUTOMATION_HEADER_SIZE;
    const uint32_t automationOffset = entryOffset + kEntryAddressBytes;
    const uint32_t modulationOffset =
        automationOffset + snapshot_codec::PROJECT_MACRO_AUTOMATION_CURVE_REF_SIZE;
    (*legacyMacroPayload)[automationOffset + kCurvePlaybackOffset] = 0;
    (*legacyMacroPayload)[automationOffset + kCurveOriginOffset] = 0;
    (*legacyMacroPayload)[modulationOffset + kCurvePlaybackOffset] = 0;
    (*legacyMacroPayload)[modulationOffset + kCurveOriginOffset] = 0;

    const auto encodedResult = snapshot_codec::encodeProjectSnapshot(
        snapshot,
        encoded->data(),
        static_cast<uint32_t>(encoded->size())
    );
    assert(encodedResult.status == project_file::Status::OK);

    std::array<project_file::DecodedChunkView, project_file::MAX_CHUNKS> decoded{};
    project_file::LoadReport containerReport{};
    const auto containerDecoded = project_file::decode(
        encoded->data(),
        encodedResult.bytesWritten,
        decoded.data(),
        static_cast<uint16_t>(decoded.size()),
        &containerReport
    );
    assert(containerDecoded.status == project_file::Status::OK);

    std::array<project_file::ChunkView, project_file::MAX_CHUNKS> legacyChunks{};
    bool foundMacroAutomation = false;
    uint16_t macroAutomationChunkIndex = project_file::MAX_CHUNKS;
    uint16_t legacyChunkCount = 0;
    for (uint16_t i = 0; i < containerDecoded.chunkCount; ++i) {
        const auto& source = decoded[i];
        if (source.id == project_file::chunkIdValue(
                project_file::ChunkId::MODULATION_GRAPH
            )) {
            continue;
        }
        auto& destination = legacyChunks[legacyChunkCount];
        destination = project_file::ChunkView{
            .id = source.id,
            .versionMajor = source.versionMajor,
            .versionMinor = source.versionMinor,
            .flags = source.flags,
            .data = source.data,
            .size = source.size,
        };
        if (source.id == project_file::chunkIdValue(
                project_file::ChunkId::MACRO_AUTOMATION
            )) {
            foundMacroAutomation = true;
            macroAutomationChunkIndex = legacyChunkCount;
            destination.versionMinor =
                snapshot_codec::PROJECT_MACRO_AUTOMATION_LEGACY_CHUNK_VERSION_MINOR;
            destination.data = legacyMacroPayload->data();
            destination.size = legacyMacroSize;
        }
        ++legacyChunkCount;
    }
    assert(foundMacroAutomation);

    const auto legacyResult = project_file::encode(
        legacyChunks.data(),
        legacyChunkCount,
        0,
        legacyEncoded->data(),
        static_cast<uint32_t>(legacyEncoded->size())
    );
    assert(legacyResult.status == project_file::Status::OK);

    project::ProjectSnapshot migrated;
    project_file::LoadReport report{};
    const auto decodeResult = snapshot_codec::decodeProjectSnapshot(
        legacyEncoded->data(),
        legacyResult.bytesWritten,
        migrated,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.loadStatus == project_file::LoadStatus::MIGRATED);
    assert(reportHas(report, project_file::LoadCode::MIGRATED_CHUNK));
    assert(migrated.projectControl);
    core::state::modulation::ProjectControlState migratedControl{};
    migratedControl.authored = *migrated.projectControl;
    const auto migratedSlot = test_support::project_control::readSlot(
        migratedControl,
        address
    );
    assert(migratedSlot.automationEnabled);
    assert(migratedSlot.compatibility.automation.modulationOrigin ==
           MacroModulationOrigin::NATIVE);
    assert(migratedSlot.modulationEnabled);
    assert(migratedSlot.compatibility.modulation.modulationOrigin ==
           MacroModulationOrigin::NATIVE);

    constexpr uint32_t kFirstAutomationPlaybackByte =
        snapshot_codec::PROJECT_MACRO_AUTOMATION_HEADER_SIZE + 4U + 1U;
    (*legacyMacroPayload)[kFirstAutomationPlaybackByte] = 1U;
    legacyChunks[macroAutomationChunkIndex].versionMinor =
        snapshot_codec::PROJECT_MACRO_AUTOMATION_LEGACY_CHUNK_VERSION_MINOR;
    const auto corruptLegacyResult = project_file::encode(
        legacyChunks.data(),
        legacyChunkCount,
        0,
        legacyEncoded->data(),
        static_cast<uint32_t>(legacyEncoded->size())
    );
    assert(corruptLegacyResult.status == project_file::Status::OK);
    project::ProjectSnapshot corruptLegacy;
    project_file::LoadReport corruptLegacyReport{};
    const auto corruptLegacyDecode = snapshot_codec::decodeProjectSnapshot(
        legacyEncoded->data(),
        corruptLegacyResult.bytesWritten,
        corruptLegacy,
        &corruptLegacyReport
    );
    assert(corruptLegacyDecode.ok);
    assert(corruptLegacyDecode.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!corruptLegacyDecode.overwriteSafe);
    assert(reportHas(corruptLegacyReport, project_file::LoadCode::CHUNK_PAYLOAD_INVALID));
    assert(reportHas(corruptLegacyReport, project_file::LoadCode::DEFAULTED_CHUNK));
    assert(corruptLegacy.projectControl);
    assert(corruptLegacy.projectControl->automation.entryCount == 0);
    assert(corruptLegacy.projectControl->modulation.sourceCount == 0);
    (*legacyMacroPayload)[kFirstAutomationPlaybackByte] = 0U;

    legacyChunks[macroAutomationChunkIndex].versionMinor = static_cast<uint8_t>(
        snapshot_codec::PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR + 1U
    );
    const auto futureResult = project_file::encode(
        legacyChunks.data(),
        legacyChunkCount,
        0,
        legacyEncoded->data(),
        static_cast<uint32_t>(legacyEncoded->size())
    );
    assert(futureResult.status == project_file::Status::OK);
    project::ProjectSnapshot future;
    project_file::LoadReport futureReport{};
    const auto futureDecode = snapshot_codec::decodeProjectSnapshot(
        legacyEncoded->data(),
        futureResult.bytesWritten,
        future,
        &futureReport
    );
    assert(futureDecode.ok);
    assert(futureDecode.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!futureDecode.overwriteSafe);
    assert(reportHas(futureReport, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(futureReport, project_file::LoadCode::DEFAULTED_CHUNK));
    assert(future.projectControl);
    assert(future.projectControl->automation.entryCount == 0);
    assert(future.projectControl->modulation.sourceCount == 0);

    std::cout
        << "[PASS] test_project_snapshot_migrates_macro_automation_v14_lifecycle_defaults\n";
}

void test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks() {
    project::ProjectState state;
    std::strncpy(state.metadata.id.data(), "p010", state.metadata.id.size() - 1);
    std::strncpy(state.metadata.name.data(), "p010", state.metadata.name.size() - 1);

    uint8_t bytes[512] = {};
    auto encodeResult = core::test::project_state_persistence::encode(
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
    assert(snapshot.projectControl);
    assert(snapshot.projectControl->automation.entryCount == 0);
    assert(snapshot.projectControl->modulation.sourceCount == 0);

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

    project::ProjectSnapshot snapshotWithoutReport;
    const auto decodeWithoutReport = snapshot_codec::decodeProjectSnapshot(
        bytes,
        encodeResult.bytesWritten,
        snapshotWithoutReport
    );
    assert(decodeWithoutReport.ok);
    assert(decodeWithoutReport.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!decodeWithoutReport.overwriteSafe);

    std::cout << "[PASS] test_project_snapshot_future_sequencer_chunk_blocks_overwrite\n";
}

void test_project_snapshot_future_or_corrupt_sequencer_envelope_blocks_overwrite() {
    test_support::CoreStorages sourceStorages;
    auto sourceState = makeCoreState(sourceStorages);
    configureProjectSession(sourceState);

    auto envelope = core::app::makeExtmemUnique<
        core::persistence::sequencer_codec::EnvelopeBuffer
    >();
    auto container = core::app::makeExtmemUnique<
        std::array<uint8_t, kProjectSnapshotScratchSize>
    >();
    assert(envelope && container);
    project::ProjectSnapshot sourceSnapshot;
    assert(project::captureProjectSnapshot(sourceState, sourceSnapshot));
    const auto encodedSequencer =
        test_support::encodeProjectSequencerSnapshot(sourceSnapshot.sequencer, *envelope);
    assert(encodedSequencer.ok);
    assert(envelope->bytes[4] ==
           core::persistence::sequencer_codec::PITCH_POLICY_ENVELOPE_VERSION);

    const auto assertUnsafe = [&](project_file::LoadCode expectedCode) {
        const project_file::ChunkView chunks[] = {{
            .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
            .versionMajor = snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = envelope->bytes.data(),
            .size = encodedSequencer.size,
        }};
        const auto encoded = project_file::encode(
            chunks,
            1,
            0,
            container->data(),
            container->size()
        );
        assert(encoded.status == project_file::Status::OK);

        project::ProjectSnapshot loaded;
        project_file::LoadReport report{};
        const auto decoded = snapshot_codec::decodeProjectSnapshot(
            container->data(),
            encoded.bytesWritten,
            loaded,
            &report
        );
        assert(decoded.ok);
        assert(decoded.loadStatus == project_file::LoadStatus::PARTIAL);
        assert(!decoded.overwriteSafe);
        assert(reportHas(report, expectedCode));
        assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));
    };

    envelope->bytes[4] = static_cast<uint8_t>(
        core::persistence::sequencer_codec::CC_LANE_ENVELOPE_VERSION + 1U
    );
    assertUnsafe(project_file::LoadCode::CHUNK_PAYLOAD_INVALID);

    envelope->bytes[4] =
        core::persistence::sequencer_codec::PITCH_POLICY_ENVELOPE_VERSION;
    envelope->bytes[10] = 1U;
    assertUnsafe(project_file::LoadCode::CHUNK_PAYLOAD_INVALID);

    std::cout
        << "[PASS] test_project_snapshot_future_or_corrupt_sequencer_envelope_blocks_overwrite\n";
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
    project::ProjectSnapshot sourceSnapshot;
    assert(project::captureProjectSnapshot(sourceState, sourceSnapshot));
    const auto encodedSequencer =
        test_support::encodeProjectSequencerSnapshot(sourceSnapshot.sequencer, *envelope);
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
    test_project_snapshot_roundtrips_sequencer_chunk_larger_than_u16();
    test_project_sequencer_snapshot_encoder_is_deterministic();
    test_project_snapshot_roundtrip_preserves_dense_macro_automation_pool();
    test_project_snapshot_rejects_invalid_project_control_references();
    test_project_snapshot_migrates_macro_automation_v14_lifecycle_defaults();
    test_project_snapshot_decode_defaults_missing_macro_and_sequencer_chunks();
    test_project_snapshot_future_sequencer_chunk_blocks_overwrite();
    test_project_snapshot_future_or_corrupt_sequencer_envelope_blocks_overwrite();
    test_project_snapshot_stale_macro_state_chunk_defaults_and_blocks_overwrite();
    test_project_snapshot_stale_sequencer_chunk_defaults_and_blocks_overwrite();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}
