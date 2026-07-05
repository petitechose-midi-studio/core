#include "state/StructureClipboardState.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM bool cloneSequencerGraph(
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

FLASHMEM void SequencerPageSelectionClipboard::reset() {
    valid = false;
    sourceFirstPage = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    count = 0;
    pages = {};
}

FLASHMEM void SequencerTrackSelectionClipboard::reset() {
    valid = false;
    count = 0;
    for (auto& entry : tracks) {
        entry.valid = false;
        entry.offset = 0;
        entry.snapshot = {};
        entry.graph.reset();
    }
}

FLASHMEM void MacroAutomationClipboard::reset() {
    valid = false;
    trackScope = false;
    sourceTrack = core::state::macro::TRACK_COUNT;
    sourcePage = core::state::macro::PAGE_COUNT;
    count = 0;
    pointPool = {};
    entries = {};
}

FLASHMEM bool MacroAutomationClipboard::append(
    uint8_t entrySourcePage,
    uint8_t entrySourceMacro,
    const core::state::macro::MacroAutomationPointPool& sourcePool,
    const core::state::macro::MacroAutomationSlotState& state
) {
    if (count >= entries.size()) return false;
    core::state::macro::MacroAutomationSlotState copied{};
    if (!core::state::macro::macroAutomationCopySlotState(
            pointPool,
            copied,
            sourcePool,
            state
        )) {
        return false;
    }
    entries[count] = MacroAutomationClipboardEntry{
        .valid = true,
        .sourcePage = entrySourcePage,
        .sourceMacro = entrySourceMacro,
        .state = copied,
    };
    count = static_cast<uint8_t>(count + 1U);
    valid = true;
    return true;
}

namespace {

FLASHMEM core::app::ExtmemUniquePtr<core::state::MacroAutomationClipboard>
makeMacroAutomationClipboard(
    const core::state::macro::MacroAutomationBankState& automation,
    uint8_t sourceTrack,
    uint8_t sourcePage,
    bool trackScope
) {
    auto clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
    if (!clipboard) return {};
    clipboard->reset();
    clipboard->trackScope = trackScope;
    clipboard->sourceTrack = sourceTrack;
    clipboard->sourcePage = sourcePage;

    const uint8_t entryCount = automation.entryCount >
            core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY
        ? core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY
        : automation.entryCount;
    for (uint8_t i = 0; i < entryCount; ++i) {
        const auto& entry = automation.entries[i];
        if (!entry.active) continue;
        if (entry.address.track != sourceTrack) continue;
        if (!trackScope && entry.address.page != sourcePage) continue;
        if (!core::state::macro::macroAutomationSlotHasContent(entry.state)) continue;
        clipboard->append(
            entry.address.page,
            entry.address.macro,
            automation.pointPool,
            entry.state
        );
    }

    return clipboard;
}

}  // namespace

FLASHMEM void StructureClipboardState::clear() {
    kind.set(StructureClipboardKind::NONE);
    macroAutomationSet.reset();
    sequencerPage.reset();
    sequencerSteps.reset();
    sequencerPageSelection.reset();
    sequencerTrackSelection.reset();
    sequencerGraph.reset();
    sequencerStepContentNodeId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    sequencerStepContentKind = SequencerStepContentClipboardKind::NONE;
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroPage(
    const core::state::macro::MacroPageData& page
) {
    macroPage = page;
    macroAutomationSet.reset();
    kind.set(StructureClipboardKind::MACRO_PAGE);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroPage(
    const core::state::macro::MacroPageData& page,
    const core::state::macro::MacroAutomationBankState& automation,
    uint8_t sourceTrack,
    uint8_t sourcePage
) {
    macroPage = page;
    macroAutomationSet = makeMacroAutomationClipboard(
        automation,
        sourceTrack,
        sourcePage,
        false
    );
    kind.set(StructureClipboardKind::MACRO_PAGE);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroTrack(
    const core::state::macro::MacroTrackData& track
) {
    macroTrack = track;
    macroAutomationSet.reset();
    kind.set(StructureClipboardKind::MACRO_TRACK);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroTrack(
    const core::state::macro::MacroTrackData& track,
    const core::state::macro::MacroAutomationBankState& automation,
    uint8_t sourceTrack
) {
    macroTrack = track;
    macroAutomationSet = makeMacroAutomationClipboard(
        automation,
        sourceTrack,
        core::state::macro::PAGE_COUNT,
        true
    );
    kind.set(StructureClipboardKind::MACRO_TRACK);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroAutomation(
    const core::state::macro::MacroAutomationBankState& automation,
    const core::state::macro::MacroAutomationSlotState& slot
) {
    auto clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
    if (!clipboard) {
        macroAutomationSet.reset();
        kind.set(StructureClipboardKind::NONE);
        revision.set(revision.get() + 1);
        return;
    }

    clipboard->reset();
    if (!clipboard->append(0, 0, automation.pointPool, slot)) {
        macroAutomationSet.reset();
        kind.set(StructureClipboardKind::NONE);
        revision.set(revision.get() + 1);
        return;
    }
    macroAutomationSet = std::move(clipboard);
    kind.set(StructureClipboardKind::MACRO_AUTOMATION);
    revision.set(revision.get() + 1);
}

FLASHMEM bool StructureClipboardState::storeSequencerPage(
    const core::state::SequencerPageClipboard& page,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!cloneSequencerGraph(sequencerGraph, graph)) {
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
    if (!cloneSequencerGraph(sequencerGraph, graph)) {
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
    if (!cloneSequencerGraph(sequencerGraph, &graph)) {
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
    if (!cloneSequencerGraph(sequencerGraph, graph)) {
        return false;
    }

    sequencerSteps = steps;
    kind.set(StructureClipboardKind::SEQUENCER_STEPS);
    revision.set(revision.get() + 1);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerPageSelection(
    const core::state::SequencerPageSelectionClipboard& pages,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!pages.valid || pages.count == 0) {
        return false;
    }
    if (!cloneSequencerGraph(sequencerGraph, graph)) {
        return false;
    }

    sequencerPageSelection = pages;
    kind.set(StructureClipboardKind::SEQUENCER_PAGE_SELECTION);
    revision.set(revision.get() + 1);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerTrackSelection(
    core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard> tracks
) {
    if (!tracks || !tracks->valid || tracks->count == 0) {
        return false;
    }

    sequencerTrackSelection = std::move(tracks);
    sequencerGraph.reset();
    kind.set(StructureClipboardKind::SEQUENCER_TRACK_SELECTION);
    revision.set(revision.get() + 1);
    return true;
}

}  // namespace core::state
