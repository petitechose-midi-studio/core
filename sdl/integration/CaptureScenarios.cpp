#include "integration/CaptureScenarios.hpp"

#include <cstdio>
#include <cstring>

#include <SDL2/SDL.h>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "state/DataManagerCatalog.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"

namespace sdl::integration {

namespace {

void publishVariationTelemetry(
    core::state::CoreState& state,
    const oc::note::sequencer::StepSequencerResolvedVariation& variation
) {
    state.sequencer.lastResolvedVariation = variation;
    state.sequencer.cycleVariationTelemetry.reset();
    state.sequencer.cycleVariationTelemetry.cycleIndex = variation.cycleIndex;
    state.sequencer.cycleVariationTelemetry.ranges = variation.ranges;
    state.sequencer.cycleVariationTelemetry.scaleSettings = variation.scaleSettings;
    state.sequencer.cycleVariationTelemetry.store(variation);
    state.sequencer.variationTelemetryRevision.set(
        state.sequencer.variationTelemetryRevision.get() + 1U
    );
}

void prepareSequencerVariationScenario(core::state::CoreState& state,
                                       core::state::sequencer::StepProperty property) {
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.activeStepProperty.set(property);
    state.sequencer.setStepDataAt(0, 60, 100, 75, 0);
    if (!state.sequencer.pattern.isEnabled(0)) {
        state.sequencer.pattern.toggle(0);
    }

    oc::note::sequencer::StepSequencerVariationRanges ranges{
        .pitchSemitones = 12,
        .velocity = 24,
        .gatePercent = 30,
        .nudge = 20,
    };
    state.sequencer.setPatternVariationRanges(ranges);

    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = 0;
    variation.cycleIndex = 1;
    variation.triggered = true;
    variation.base = {.note = 60, .velocity = 100, .gate = 75, .nudge = 0};
    variation.resolved = variation.base;
    variation.ranges = ranges;
    variation.pitchDelta = 4;
    variation.velocityDelta = -12;
    variation.gateDelta = 18;
    variation.nudgeDelta = -8;
    variation.resolved.note = 64;
    variation.resolved.velocity = 88;
    variation.resolved.gate = 93;
    variation.resolved.nudge = -8;

    publishVariationTelemetry(state, variation);
}

void prepareSequencerScaleScenario(
    core::state::CoreState& state,
    oc::note::sequencer::StepSequencerScaleConstraintMode mode
) {
    using namespace oc::note::sequencer;

    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::NOTE);
    state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);
    state.sequencer.setStepDataAt(0, 61, 100, 75, 0);
    if (!state.sequencer.pattern.isEnabled(0)) {
        state.sequencer.pattern.toggle(0);
    }

    StepSequencerScaleSettings settings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = mode,
    };
    state.sequencerTracks.setProjectScaleSettings(settings);
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );

    const StepSequencerVariationRanges ranges{};
    const auto variation = resolveStepVariation(
        StepSequencerStepValues{.note = 61, .velocity = 100, .gate = 75, .nudge = 0},
        ranges,
        settings,
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT,
        0U,
        1U,
        0U,
        true
    );
    publishVariationTelemetry(state, variation);
}

void enableSequencerStep(core::state::sequencer::SequencerState& sequencer, uint8_t step) {
    if (!sequencer.pattern.isEnabled(step)) {
        sequencer.pattern.toggle(step);
    }
}

void offsetFirstMicroStep(core::state::sequencer::SequencerState& sequencer,
                          uint16_t sequenceId,
                          int8_t noteOffset) {
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* sequence = graph ? graph->sequence(sequenceId) : nullptr;
    if (sequence == nullptr) return;
    core::state::sequencer::setNodeNoteOffset(
        sequencer.pattern,
        sequence->firstStepNode,
        noteOffset
    );
}

void offsetFirstCycleState(core::state::sequencer::SequencerState& sequencer,
                           uint16_t cycleSetId,
                           int8_t noteOffset) {
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* cycleSet = graph ? graph->cycleSet(cycleSetId) : nullptr;
    if (cycleSet == nullptr) return;
    core::state::sequencer::setNodeNoteOffset(
        sequencer.pattern,
        cycleSet->firstStateNode,
        noteOffset
    );
}

void prepareMacroAutomationCleanScenario(core::state::CoreState& state) {
    state.activeView.set(core::ui::ViewType::MACRO);
    state.overlays.hideAll();
    state.pages.initDefaults();
    state.pages.automation.clear();
    state.macroUi.reset();
    state.macroEdit.reset();
    state.trackNavigation.reset();
    state.structureClipboard.clear();
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

void prepareSequencerSemanticGridScenario(core::state::CoreState& state) {
    using namespace oc::note::sequencer;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(8);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::NOTE);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Chromatic,
        .mode = StepSequencerScaleConstraintMode::Free,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );

    state.sequencer.setStepDataAt(0, 60, 104, 70, 0, 100);
    state.sequencer.setStepDataAt(1, 62, 96, 80, 0, 65);
    state.sequencer.setStepDataAt(2, 64, 100, 75, 0, 100);
    state.sequencer.setStepDataAt(3, 65, 100, 75, 0, 100);
    state.sequencer.setStepDataAt(4, 67, 110, 90, -10, 70);
    state.sequencer.setStepDataAt(5, 69, 84, 45, -18, 100);
    state.sequencer.setStepDataAt(6, 71, 122, 120, 12, 100);

    for (uint8_t step = 0; step < 7; ++step) {
        enableSequencerStep(state.sequencer, step);
    }

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (micro.ok) {
        offsetFirstMicroStep(state.sequencer, micro.id, 3);
    }

    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(3),
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );
    if (cycle.ok) {
        offsetFirstCycleState(state.sequencer, cycle.id, 5);
    }

    const auto nestedMicro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(4),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (nestedMicro.ok) {
        offsetFirstMicroStep(state.sequencer, nestedMicro.id, 4);
    }
    const auto nestedCycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(4),
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );
    if (nestedCycle.ok) {
        offsetFirstCycleState(state.sequencer, nestedCycle.id, -2);
    }
}

void prepareSequencerLocalRandomGridScenario(core::state::CoreState& state) {
    using namespace oc::note::sequencer;
    using core::state::sequencer::StepProperty;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(8);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Chromatic,
        .mode = StepSequencerScaleConstraintMode::Free,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );

    state.sequencer.setStepDataAt(0, 60, 104, 75, 0, 100);
    state.sequencer.setStepDataAt(1, 62, 96, 75, 0, 100);
    state.sequencer.setStepDataAt(2, 64, 100, 75, 0, 100);
    state.sequencer.setStepDataAt(3, 65, 84, 70, 0, 100);

    for (uint8_t step = 0; step < 4; ++step) {
        enableSequencerStep(state.sequencer, step);
    }

    core::state::sequencer::setNodeLocalVariationRange(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(0),
        StepProperty::NOTE,
        5
    );

    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(1),
        2
    );
    if (cycle.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* cycleSet = graph ? graph->cycleSet(cycle.id) : nullptr;
        if (cycleSet != nullptr) {
            const auto stateNode = cycleSet->firstStateNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, stateNode, 2);
            core::state::sequencer::setNodeLocalVariationRange(
                state.sequencer.pattern,
                stateNode,
                StepProperty::NOTE,
                6
            );
        }
    }

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (micro.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* sequence = graph ? graph->sequence(micro.id) : nullptr;
        if (sequence != nullptr) {
            const auto microNode = sequence->firstStepNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, microNode, 3);
            core::state::sequencer::setNodeLocalVariationRange(
                state.sequencer.pattern,
                microNode,
                StepProperty::NOTE,
                4
            );
        }
    }

    state.sequencer.probabilityCycleIndex = 0;
}

void setAllLocalVariationRanges(
    core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges
) {
    using core::state::sequencer::StepProperty;

    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::NOTE,
        ranges.pitchSemitones
    );
    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::VELOCITY,
        ranges.velocity
    );
    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::GATE,
        ranges.gatePercent
    );
    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::NUDGE,
        ranges.nudge
    );
}

void prepareSequencerSummedLocalRandomScenario(
    core::state::CoreState& state,
    core::state::sequencer::StepProperty activeProperty
) {
    using namespace oc::note::sequencer;
    using core::state::sequencer::StepProperty;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(8);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(activeProperty);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Chromatic,
        .mode = StepSequencerScaleConstraintMode::Free,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    state.sequencer.setPatternVariationRanges({
        .pitchSemitones = 2,
        .velocity = 12,
        .gatePercent = 18,
        .nudge = 5,
    });

    state.sequencer.setStepDataAt(0, 60, 80, 70, 0, 100);
    state.sequencer.setStepDataAt(1, 62, 92, 85, 0, 100);
    state.sequencer.setStepDataAt(2, 64, 72, 60, -8, 100);
    state.sequencer.setStepDataAt(3, 65, 108, 95, 10, 100);

    for (uint8_t step = 0; step < 4; ++step) {
        enableSequencerStep(state.sequencer, step);
    }

    setAllLocalVariationRanges(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(0),
        {
            .pitchSemitones = 4,
            .velocity = 18,
            .gatePercent = 14,
            .nudge = 7,
        }
    );

    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(1),
        2
    );
    if (cycle.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* cycleSet = graph ? graph->cycleSet(cycle.id) : nullptr;
        if (cycleSet != nullptr) {
            const auto stateNode = cycleSet->firstStateNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, stateNode, 2);
            core::state::sequencer::setNodeVelocityOffset(state.sequencer.pattern, stateNode, -8);
            core::state::sequencer::setNodeGateOffset(state.sequencer.pattern, stateNode, -12);
            core::state::sequencer::setNodeNudgeOffset(state.sequencer.pattern, stateNode, 5);
            setAllLocalVariationRanges(
                state.sequencer.pattern,
                stateNode,
                {
                    .pitchSemitones = 5,
                    .velocity = 16,
                    .gatePercent = 12,
                    .nudge = 6,
                }
            );
        }
    }

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (micro.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* sequence = graph ? graph->sequence(micro.id) : nullptr;
        if (sequence != nullptr) {
            const auto microNode = sequence->firstStepNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, microNode, -3);
            core::state::sequencer::setNodeVelocityOffset(state.sequencer.pattern, microNode, 10);
            core::state::sequencer::setNodeGateOffset(state.sequencer.pattern, microNode, 16);
            core::state::sequencer::setNodeNudgeOffset(state.sequencer.pattern, microNode, -6);
            setAllLocalVariationRanges(
                state.sequencer.pattern,
                microNode,
                {
                    .pitchSemitones = 3,
                    .velocity = 20,
                    .gatePercent = 15,
                    .nudge = 9,
                }
            );
        }
    }

    state.sequencer.probabilityCycleIndex = 0;
}

void prepareSequencerNestedLocalRandomRuntimeScenario(core::state::CoreState& state) {
    using namespace oc::note::sequencer;
    using core::state::sequencer::StepProperty;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(4);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    state.sequencer.setPatternVariationRanges({
        .pitchSemitones = 2,
        .velocity = 10,
        .gatePercent = 12,
        .nudge = 4,
    });

    state.sequencer.setStepDataAt(0, 60, 96, 85, 0, 100);
    enableSequencerStep(state.sequencer, 0);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        rootNode,
        2
    );
    if (!cycle.ok) return;

    core::state::sequencer::setCycleStateSetOffset(state.sequencer.pattern, cycle.id, -1);
    const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
    const auto* cycleSet = graph ? graph->cycleSet(cycle.id) : nullptr;
    if (cycleSet == nullptr) return;

    const auto activeStateNode = static_cast<core::state::sequencer::SequencerGraphNodeId>(
        cycleSet->firstStateNode + 1U
    );
    core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, activeStateNode, 1);
    core::state::sequencer::setNodeVelocityOffset(state.sequencer.pattern, activeStateNode, -6);
    setAllLocalVariationRanges(
        state.sequencer.pattern,
        activeStateNode,
        {
            .pitchSemitones = 6,
            .velocity = 18,
            .gatePercent = 10,
            .nudge = 5,
        }
    );

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        activeStateNode,
        2
    );
    if (micro.ok) {
        graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* sequence = graph ? graph->sequence(micro.id) : nullptr;
        if (sequence != nullptr) {
            core::state::sequencer::setNodeNoteOffset(
                state.sequencer.pattern,
                sequence->firstStepNode,
                0
            );
            core::state::sequencer::setNodeNoteOffset(
                state.sequencer.pattern,
                static_cast<core::state::sequencer::SequencerGraphNodeId>(
                    sequence->firstStepNode + 1U
                ),
                2
            );
        }
    }

    core::state::sequencer::enterCycleStatesContentView(
        state.sequencer,
        rootNode,
        cycle.id
    );
    state.sequencer.probabilityCycleIndex = 0;
}

}  // namespace

bool applyCaptureScenario(core::state::CoreState& state, const char* scenario) {
    if (!scenario || scenario[0] == '\0' || std::strcmp(scenario, "macro") == 0) {
        return true;
    }

    if (std::strcmp(scenario, "macro-automation-clean") == 0) {
        prepareMacroAutomationCleanScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-edit") == 0) {
        const auto& config = core::state::macro::MacroWorkflow::activeConfig(state.pages, 0);
        state.overlays.show(core::ui::OverlayType::MACRO_EDIT, false);
        state.macroEdit.openEditor(0, config.channel, config.cc, SDL_GetTicks());
        return true;
    }

    if (std::strcmp(scenario, "macro-page-selector") == 0) {
        state.overlays.show(core::ui::OverlayType::PAGE_SELECTOR, false);
        state.pages.selector.selectedIndex.set(state.pages.currentActivePage());
        return true;
    }

    if (std::strcmp(scenario, "view-selector") == 0) {
        state.overlays.show(core::ui::OverlayType::VIEW_SELECTOR, false);
        state.viewSelector.selectedIndex.set(static_cast<int>(state.activeView.get()));
        return true;
    }

    if (std::strcmp(scenario, "sequencer") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        return true;
    }

    if (std::strcmp(scenario, "seq-step-edit") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        state.sequencer.setStepDataAt(0, 60, 100, 75);
        if (!state.sequencer.pattern.isEnabled(0)) {
            state.sequencer.pattern.toggle(0);
        }
        state.overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT, false);
        state.sequencer.stepEdit.stepIndex.set(0);
        state.sequencer.stepEdit.focusedRow.set(0);
        return true;
    }

    if (std::strcmp(scenario, "seq-property-selector") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        state.sequencer.stepPropertyInlineSelector.selecting.set(true);
        state.sequencer.stepPropertyInlineSelector.selectedIndex.set(
            static_cast<int>(core::state::sequencer::StepProperty::GATE)
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-quick-controls") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        state.sequencer.patternQuickControls.selecting.set(true);
        state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::NOTE);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-pitch") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::NOTE);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-velocity") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::VELOCITY);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-gate") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::GATE);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-nudge") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::NUDGE);
        return true;
    }

    if (std::strcmp(scenario, "seq-scale-constrained-degree") == 0) {
        prepareSequencerScaleScenario(
            state,
            oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-scale-free-out-of-scale") == 0) {
        prepareSequencerScaleScenario(
            state,
            oc::note::sequencer::StepSequencerScaleConstraintMode::Free
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-semantic-grid") == 0) {
        prepareSequencerSemanticGridScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-grid") == 0) {
        prepareSequencerLocalRandomGridScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-pitch") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::NOTE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-velocity") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::VELOCITY
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-gate") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::GATE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-nudge") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::NUDGE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-nested-runtime") == 0) {
        prepareSequencerNestedLocalRandomRuntimeScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "settings") == 0) {
        state.activeView.set(core::ui::ViewType::DEVICE_SETTINGS);
        state.deviceSettings.openView();
        return true;
    }

    if (std::strcmp(scenario, "data-manager") == 0) {
        state.overlays.show(core::ui::OverlayType::DATA_MANAGER, false);
        state.dataManager.openSession(
            state.activeView.get() == core::ui::ViewType::SEQUENCER
                ? core::state::DataManagerContext::SEQUENCER
                : core::state::DataManagerContext::MACRO
        );
        return true;
    }

    if (std::strcmp(scenario, "data-manager-dialog") == 0) {
        state.overlays.show(core::ui::OverlayType::DATA_MANAGER, false);
        state.dataManager.openSession(core::state::DataManagerContext::MACRO);
        state.overlays.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
        state.dataManager.showDialog(core::state::DataManagerDialogMode::COMMAND_PALETTE, 0);
        return true;
    }

    std::fprintf(stderr, "Unknown capture scenario: %s\n", scenario);
    return false;
}

void tickFrames(::sdl::SdlEnvironment& env,
                oc::app::OpenControlApp& app,
                core::state::CoreState& state,
                int frames) {
    if (frames <= 0) frames = 1;
    for (int i = 0; i < frames; ++i) {
        env.processEvents();
        app.update();
        state.update();
        env.refresh();
        SDL_Delay(16);
    }
}

::sdl::ScreenshotScope captureScopeFromArg(const char* value) {
    if (value && std::strcmp(value, "screen") == 0) {
        return ::sdl::ScreenshotScope::Screen;
    }
    return ::sdl::ScreenshotScope::Controller;
}

}  // namespace sdl::integration
