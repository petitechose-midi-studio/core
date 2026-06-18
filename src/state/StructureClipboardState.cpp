#include "state/StructureClipboardState.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::state {

namespace {

FLASHMEM bool storeSequencerGraph(
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>& target,
    const oc::note::sequencer::StepSequencerGraph* source
) {
    if (source == nullptr || !source->enabled) {
        target.reset();
        return true;
    }

    auto graph = core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>();
    if (!graph) {
        return false;
    }

    *graph = *source;
    target = std::move(graph);
    return true;
}

}  // namespace

FLASHMEM void SequencerPageClipboard::reset() {
    valid = false;
    sourcePage = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    count = 0;
    enabledMask = 0;
}

FLASHMEM void SequencerStepsClipboard::reset() {
    valid = false;
    rootContext = true;
    count = 0;
    span = 0;
    entries = {};
}

FLASHMEM void StructureClipboardState::clear() {
    kind.set(StructureClipboardKind::NONE);
    sequencerPage.reset();
    sequencerSteps.reset();
    sequencerGraph.reset();
    sequencerStepContentNodeId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    sequencerStepContentKind = SequencerStepContentClipboardKind::NONE;
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroPage(
    const core::state::macro::MacroPageData& page
) {
    macroPage = page;
    kind.set(StructureClipboardKind::MACRO_PAGE);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroTrack(
    const core::state::macro::MacroTrackData& track
) {
    macroTrack = track;
    kind.set(StructureClipboardKind::MACRO_TRACK);
    revision.set(revision.get() + 1);
}

FLASHMEM bool StructureClipboardState::storeSequencerPage(
    const core::state::SequencerPageClipboard& page,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!storeSequencerGraph(sequencerGraph, graph)) {
        return false;
    }

    sequencerPage = page;
    kind.set(StructureClipboardKind::SEQUENCER_PAGE);
    revision.set(revision.get() + 1);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerTrack(
    const core::state::sequencer::SequencerPatternSnapshot& track,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!storeSequencerGraph(sequencerGraph, graph)) {
        return false;
    }

    sequencerTrack = track;
    kind.set(StructureClipboardKind::SEQUENCER_TRACK);
    revision.set(revision.get() + 1);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerStepContent(
    const oc::note::sequencer::StepSequencerGraph& graph,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    SequencerStepContentClipboardKind contentKind
) {
    if (!storeSequencerGraph(sequencerGraph, &graph)) {
        return false;
    }

    sequencerStepContentNodeId = nodeId;
    sequencerStepContentKind = contentKind;
    kind.set(StructureClipboardKind::SEQUENCER_STEP_CONTENT);
    revision.set(revision.get() + 1);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerSteps(
    const core::state::SequencerStepsClipboard& steps,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!steps.valid || steps.count == 0) {
        return false;
    }
    if (!storeSequencerGraph(sequencerGraph, graph)) {
        return false;
    }

    sequencerSteps = steps;
    kind.set(StructureClipboardKind::SEQUENCER_STEPS);
    revision.set(revision.get() + 1);
    return true;
}

}  // namespace core::state
