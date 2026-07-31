#include "state/sequencer/SequencerExpansionBudgetProjection.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/note/sequencer/StepSequencerExpander.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::state::sequencer {
namespace {

struct ExpansionBudgetWorkspace {
    oc::note::sequencer::StepSequencerRuntimeState runtime{};
    oc::note::sequencer::StepSequencerGraph draftGraph{};
};

// The projection is serialized on the UI thread. Keeping its graph and runtime
// scratch in PSRAM avoids a ~17 KiB stack/RAM1 cost on every Chord render.
EXTMEM ExpansionBudgetWorkspace workspace{};

FLASHMEM uint8_t owningRootStep(
    const SequencerState& sequencer,
    uint8_t activeContentStep
) {
    if (!sequencer.contentView.isChildContent()) {
        return activeContentStep;
    }
    const auto* frame = sequencer.contentView.currentFrame();
    return frame != nullptr ? frame->ownerRootStep : activeContentStep;
}

FLASHMEM void prepareRuntimeState(
    oc::note::sequencer::StepSequencerRuntimeState& runtime,
    const SequencerPatternState& pattern,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    uint8_t rootStep
) {
    runtime.reset();
    runtime.length = pattern.length.get();
    runtime.stepsPerBeat = pattern.stepsPerBeat.get();
    runtime.enabledMask = pattern.enabledMask.get();
    // This is an authoring-capacity preview. Show what will happen when the
    // edited root is enabled, independently of its current mute/probability.
    runtime.enabledMask.setBit(rootStep, true);
    runtime.scaleSettings = resolveEffectiveScaleSettings(
        projectScaleSettings,
        pattern.scalePolicy,
        pattern.scaleOverride
    );
    runtime.variationRanges = pattern.variationRanges;

    std::copy(pattern.note.begin(), pattern.note.end(), runtime.note.begin());
    std::copy(pattern.velocity.begin(), pattern.velocity.end(), runtime.velocity.begin());
    std::copy(pattern.gate.begin(), pattern.gate.end(), runtime.gate.begin());
    std::copy(pattern.nudge.begin(), pattern.nudge.end(), runtime.nudge.begin());
    // A probability gate must not hide a structural expansion overflow from
    // the editor. Explicit graph mutes and zero gates remain authoritative.
    runtime.probability.fill(100);
}

FLASHMEM uint8_t ticksPerStep(uint8_t stepsPerBeat) {
    const uint8_t safeStepsPerBeat = std::clamp<uint8_t>(
        stepsPerBeat == 0
            ? oc::note::sequencer::StepSequencerRuntimeState::DEFAULT_STEPS_PER_BEAT
            : stepsPerBeat,
        1U,
        static_cast<uint8_t>(oc::note::clock::PPQN)
    );
    return std::max<uint8_t>(
        1U,
        static_cast<uint8_t>(oc::note::clock::PPQN / safeStepsPerBeat)
    );
}

}  // namespace

FLASHMEM SequencerExpansionBudgetProjection projectSequencerExpansionBudget(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    uint8_t activeContentStep
) {
    const auto& pattern = authoringPattern(sequencer);
    const uint8_t rootStep = owningRootStep(sequencer, activeContentStep);
    if (rootStep >= pattern.length.get() ||
        rootStep >= oc::note::sequencer::StepSequencerRuntimeState::MAX_STEPS) {
        return {};
    }

    const oc::note::sequencer::StepSequencerGraph* graph = graphView(pattern);
    if (sequencer.stepContentDraft.active.get() &&
        sequencer.stepContentDraft.kind.get() ==
            SequencerStepContentDraftKind::CHORD) {
        if (!captureStepContentDraftRuntimeGraph(sequencer, workspace.draftGraph)) {
            return {};
        }
        graph = &workspace.draftGraph;
    }
    if (graph == nullptr || !graph->enabled) {
        return {
            .valid = true,
            .rootStepIndex = rootStep,
        };
    }

    prepareRuntimeState(
        workspace.runtime,
        pattern,
        projectScaleSettings,
        rootStep
    );
    const auto analysis =
        oc::note::sequencer::StepSequencerExpander::analyzeRootStep(
            workspace.runtime,
            *graph,
            rootStep,
            0,
            ticksPerStep(workspace.runtime.stepsPerBeat),
            0,
            true
        );
    return {
        .valid = true,
        .rootStepIndex = rootStep,
        .emittedNoteCount = analysis.emittedNoteCount,
        .requestedNoteCount = analysis.requestedNoteCount,
        .noteBudgetExceeded = analysis.noteBudgetExceeded,
        .depthLimitReached = analysis.depthLimitReached,
    };
}

}  // namespace core::state::sequencer
