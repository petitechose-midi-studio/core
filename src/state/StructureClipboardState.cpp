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

    auto graph = core::app::makeExtmemUnique<
        oc::note::sequencer::StepSequencerGraph
    >(*source);
    if (!graph) return false;
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
        entry.sourceTrack = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
        entry.snapshot = {};
        entry.graph.reset();
        entry.ccLanes.reset();
    }
}

FLASHMEM MacroAutomationClipboard::MacroAutomationClipboard() = default;

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

FLASHMEM void releaseOwnedPayloads(core::state::StructureClipboardState& clipboard) {
    clipboard.macroAutomationSet.reset();
    clipboard.sequencerTrackSelection.reset();
    clipboard.sequencerGraph.reset();
    clipboard.sequencerCcLanes.reset();
    clipboard.sequencerTrackSource =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
}

FLASHMEM void commitClipboardKind(
    core::state::StructureClipboardState& clipboard,
    core::state::StructureClipboardKind kind
) {
    clipboard.kind.set(kind);
    clipboard.revision.set(clipboard.revision.get() + 1U);
}

FLASHMEM bool rejectClipboardStore(core::state::StructureClipboardState& clipboard) {
    clipboard.clear();
    return false;
}

struct MacroAutomationClipboardBuild {
    core::app::ExtmemUniquePtr<core::state::MacroAutomationClipboard> value;
    bool success = true;
};

FLASHMEM MacroAutomationClipboardBuild
makeMacroAutomationClipboard(
    const core::state::macro::MacroAutomationBankState& automation,
    uint8_t sourceTrack,
    uint8_t sourcePage,
    bool trackScope
) {
    core::app::ExtmemUniquePtr<core::state::MacroAutomationClipboard> clipboard;

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
        if (!clipboard) {
            clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
            if (!clipboard) return {.success = false};
            clipboard->trackScope = trackScope;
            clipboard->sourceTrack = sourceTrack;
            clipboard->sourcePage = sourcePage;
        }
        if (!clipboard->append(
            entry.address.page,
            entry.address.macro,
            automation.pointPool,
            entry.state
        )) {
            return {.success = false};
        }
    }

    return {.value = std::move(clipboard)};
}

}  // namespace

FLASHMEM void StructureClipboardState::clear() {
    kind.set(StructureClipboardKind::NONE);
    releaseOwnedPayloads(*this);
    sequencerPage.reset();
    sequencerSteps.reset();
    sequencerPageSelection.reset();
    sequencerStepContentNodeId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    sequencerStepContentKind = SequencerStepContentClipboardKind::NONE;
    revision.set(revision.get() + 1U);
}

FLASHMEM bool StructureClipboardState::storeMacroPage(
    const core::state::macro::MacroPageData& page,
    const core::state::macro::MacroAutomationBankState& automation,
    uint8_t sourceTrack,
    uint8_t sourcePage
) {
    auto automationSet = makeMacroAutomationClipboard(
        automation,
        sourceTrack,
        sourcePage,
        false
    );
    if (!automationSet.success) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    macroPage = page;
    macroAutomationSet = std::move(automationSet.value);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_PAGE);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroTrack(
    const core::state::macro::MacroTrackData& track,
    const core::state::macro::MacroAutomationBankState& automation,
    uint8_t sourceTrack
) {
    auto automationSet = makeMacroAutomationClipboard(
        automation,
        sourceTrack,
        core::state::macro::PAGE_COUNT,
        true
    );
    if (!automationSet.success) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    macroTrack = track;
    macroAutomationSet = std::move(automationSet.value);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_TRACK);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroAutomation(
    const core::state::macro::MacroAutomationBankState& automation,
    const core::state::macro::MacroAutomationSlotState& slot
) {
    if (!core::state::macro::macroAutomationSlotHasContent(slot)) {
        return rejectClipboardStore(*this);
    }

    auto clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
    if (!clipboard) {
        return rejectClipboardStore(*this);
    }

    if (!clipboard->append(0, 0, automation.pointPool, slot)) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    clipboard->payloadKind = MacroClipboardPayloadKind::LEGACY_AUTOMATION;
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_AUTOMATION);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroSlot(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    if (!core::state::macro::macroAutomationAddressValid(address)) {
        return rejectClipboardStore(*this);
    }

    const auto& page = pages.pageData(address.track, address.page);
    if (!page.isMacroActive(address.macro)) {
        return rejectClipboardStore(*this);
    }

    auto clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
    if (!clipboard) return rejectClipboardStore(*this);
    clipboard->payloadKind = MacroClipboardPayloadKind::SLOT;
    clipboard->sourceTrack = address.track;
    clipboard->sourcePage = address.page;
    clipboard->sourceMacro = address.macro;
    clipboard->sourceMacroActive = true;
    clipboard->sourceCc = page.cc[address.macro];
    clipboard->sourceStaticValue = page.values[address.macro];

    const auto* slot = core::state::macro::macroAutomationFindSlot(
        pages.automation,
        address
    );
    clipboard->sourceSlotPresent = slot != nullptr;
    const core::state::macro::MacroAutomationSlotState empty{};
    if (!clipboard->append(
            address.page,
            address.macro,
            pages.automation.pointPool,
            slot != nullptr ? *slot : empty
        )) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_SLOT);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroModulation(
    const core::state::macro::MacroAutomationBankState& automation,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        automation,
        address
    );
    if (slot == nullptr ||
        !core::state::macro::macroCurveStored(slot->modulation)) {
        return rejectClipboardStore(*this);
    }

    auto clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
    if (!clipboard) return rejectClipboardStore(*this);
    clipboard->payloadKind = MacroClipboardPayloadKind::MODULATION;
    clipboard->sourceTrack = address.track;
    clipboard->sourcePage = address.page;
    clipboard->sourceMacro = address.macro;
    clipboard->sourceSlotPresent = true;
    core::state::macro::MacroAutomationSlotState modulationOnly{};
    modulationOnly.modulation = slot->modulation;
    modulationOnly.modulationDepth = slot->modulationDepth;
    if (!clipboard->append(
            address.page,
            address.macro,
            automation.pointPool,
            modulationOnly
        )) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_MODULATION);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerPage(
    const core::state::SequencerPageClipboard& page,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!page.valid || page.count == 0) {
        return rejectClipboardStore(*this);
    }
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    if (!cloneSequencerGraph(graphCopy, graph)) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    sequencerPage = page;
    sequencerGraph = std::move(graphCopy);
    commitClipboardKind(*this, StructureClipboardKind::SEQUENCER_PAGE);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerTrack(
    const core::state::sequencer::SequencerPatternSnapshot& track,
    const oc::note::sequencer::StepSequencerGraph* graph,
    uint8_t sourceTrack,
    const core::state::sequencer::SequencerCcLaneBank* ccLanes
) {
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    core::state::sequencer::SequencerCcLaneBankPtr ccLaneCopy;
    if (!cloneSequencerGraph(graphCopy, graph) ||
        !core::state::sequencer::cloneSequencerCcLaneBank(
            ccLaneCopy,
            ccLanes
        )) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    sequencerTrack = track;
    sequencerTrackSource = sourceTrack < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        ? sourceTrack
        : core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    sequencerGraph = std::move(graphCopy);
    sequencerCcLanes = std::move(ccLaneCopy);
    commitClipboardKind(*this, StructureClipboardKind::SEQUENCER_TRACK);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerStepContent(
    const oc::note::sequencer::StepSequencerGraph& graph,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    SequencerStepContentClipboardKind contentKind
) {
    if (nodeId == oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID ||
        contentKind == SequencerStepContentClipboardKind::NONE) {
        return rejectClipboardStore(*this);
    }
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    if (!cloneSequencerGraph(graphCopy, &graph)) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    sequencerGraph = std::move(graphCopy);
    sequencerStepContentNodeId = nodeId;
    sequencerStepContentKind = contentKind;
    commitClipboardKind(*this, StructureClipboardKind::SEQUENCER_STEP_CONTENT);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerSteps(
    const core::state::SequencerStepsClipboard& steps,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!steps.valid || steps.count == 0) {
        return rejectClipboardStore(*this);
    }
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    if (!cloneSequencerGraph(graphCopy, graph)) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    sequencerSteps = steps;
    sequencerGraph = std::move(graphCopy);
    commitClipboardKind(*this, StructureClipboardKind::SEQUENCER_STEPS);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerPageSelection(
    const core::state::SequencerPageSelectionClipboard& pages,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    if (!pages.valid || pages.count == 0) {
        return rejectClipboardStore(*this);
    }
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    if (!cloneSequencerGraph(graphCopy, graph)) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    sequencerPageSelection = pages;
    sequencerGraph = std::move(graphCopy);
    commitClipboardKind(*this, StructureClipboardKind::SEQUENCER_PAGE_SELECTION);
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerTrackSelection(
    core::app::ExtmemUniquePtr<core::state::SequencerTrackSelectionClipboard> tracks
) {
    if (!tracks || !tracks->valid || tracks->count == 0) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    sequencerTrackSelection = std::move(tracks);
    commitClipboardKind(*this, StructureClipboardKind::SEQUENCER_TRACK_SELECTION);
    return true;
}

}  // namespace core::state
