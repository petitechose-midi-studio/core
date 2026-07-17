#include "state/StructureClipboardState.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

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
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    bool includeAutomation,
    bool includeModulation
) {
    if (count >= entries.size()) return false;
    core::state::modulation::ProjectControlMacroSlotView view{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            control,
            address,
            view
        ) || view.compatibilityMutationAmbiguous ||
        (view.modulationStored && !view.primaryRecordedShape)) {
        return false;
    }

    auto copied = view.compatibility;
    const uint16_t automationCount = includeAutomation && view.automationStored
        ? copied.automation.pointCount
        : 0U;
    const uint16_t modulationCount = includeModulation && view.modulationStored
        ? copied.modulation.pointCount
        : 0U;
    const uint32_t required = static_cast<uint32_t>(automationCount) +
                              modulationCount;
    if (required > static_cast<uint32_t>(
            core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY -
            pointPool.used
        )) {
        return false;
    }

    const uint16_t start = pointPool.used;
    uint16_t cursor = start;
    if (automationCount > 0U) {
        for (uint16_t index = 0; index < automationCount; ++index) {
            const auto& source = control.authored.curves.points[
                copied.automation.pointOffset + index
            ];
            pointPool.points[cursor + index] = {
                .tick = source.tick,
                .value = source.value,
            };
        }
        copied.automation.pointOffset = cursor;
        cursor = static_cast<uint16_t>(cursor + automationCount);
    } else {
        copied.automation = {};
    }
    if (modulationCount > 0U) {
        for (uint16_t index = 0; index < modulationCount; ++index) {
            const auto& source = control.authored.curves.points[
                copied.modulation.pointOffset + index
            ];
            pointPool.points[cursor + index] = {
                .tick = source.tick,
                .value = source.value,
            };
        }
        copied.modulation.pointOffset = cursor;
        cursor = static_cast<uint16_t>(cursor + modulationCount);
    } else {
        copied.modulation = {};
        copied.modulationDepth = 0.0f;
    }
    pointPool.used = cursor;
    entries[count] = MacroAutomationClipboardEntry{
        .valid = true,
        .sourcePage = entrySourcePage,
        .sourceMacro = entrySourceMacro,
        .state = copied,
        .destinationScaleQ15 = includeModulation
            ? core::state::modulation::projectModulationDestinationScaleQ15(
                  control.authored.modulation,
                  core::state::modulation::projectControlDestination(address)
              )
            : core::state::modulation::
                  PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15,
    };
    count = static_cast<uint8_t>(count + 1U);
    valid = true;
    return true;
}

namespace {

FLASHMEM void releaseOwnedPayloads(core::state::StructureClipboardState& clipboard) {
    clipboard.macroAutomationSet.reset();
    clipboard.macroModulationAssignment.reset();
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
    const core::state::modulation::ProjectControlState& control,
    uint8_t sourceTrack,
    uint8_t sourcePage,
    bool trackScope
) {
    core::app::ExtmemUniquePtr<core::state::MacroAutomationClipboard> clipboard;
    const uint8_t firstPage = trackScope ? 0U : sourcePage;
    const uint8_t endPage = trackScope
        ? core::state::macro::PAGE_COUNT
        : static_cast<uint8_t>(sourcePage + 1U);
    for (uint8_t page = firstPage; page < endPage; ++page) {
        for (uint8_t macro = 0; macro < core::state::macro::MACRO_COUNT; ++macro) {
            const core::state::macro::MacroAutomationSlotAddress address{
                sourceTrack,
                page,
                macro,
            };
            core::state::modulation::ProjectControlMacroSlotView view{};
            if (!core::state::modulation::readProjectControlMacroSlot(
                    control,
                    address,
                    view
                )) {
                return {.success = false};
            }
            if (!view.present) continue;
            if (!clipboard) {
                clipboard = core::app::makeExtmemUnique<
                    core::state::MacroAutomationClipboard
                >();
                if (!clipboard) return {.success = false};
                clipboard->trackScope = trackScope;
                clipboard->sourceTrack = sourceTrack;
                clipboard->sourcePage = sourcePage;
            }
            if (!clipboard->append(page, macro, control, address)) {
                return {.success = false};
            }
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
    projectModulatorSource = {};
    sequencerStepContentNodeId = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    sequencerStepContentKind = SequencerStepContentClipboardKind::NONE;
    revision.set(revision.get() + 1U);
}

FLASHMEM bool StructureClipboardState::storeMacroPage(
    const core::state::macro::MacroPageData& page,
    const core::state::modulation::ProjectControlState& control,
    uint8_t sourceTrack,
    uint8_t sourcePage
) {
    auto automationSet = makeMacroAutomationClipboard(
        control,
        sourceTrack,
        sourcePage,
        false
    );
    if (!automationSet.success) return rejectClipboardStore(*this);
    releaseOwnedPayloads(*this);
    macroPage = page;
    macroAutomationSet = std::move(automationSet.value);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_PAGE);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroTrack(
    const core::state::macro::MacroTrackData& track,
    const core::state::modulation::ProjectControlState& control,
    uint8_t sourceTrack
) {
    auto automationSet = makeMacroAutomationClipboard(
        control,
        sourceTrack,
        core::state::macro::PAGE_COUNT,
        true
    );
    if (!automationSet.success) return rejectClipboardStore(*this);
    releaseOwnedPayloads(*this);
    macroTrack = track;
    macroAutomationSet = std::move(automationSet.value);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_TRACK);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroAutomation(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    core::state::modulation::ProjectControlMacroSlotView view{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            control,
            address,
            view
        ) || !view.automationStored) {
        return rejectClipboardStore(*this);
    }
    auto clipboard = core::app::makeExtmemUnique<
        core::state::MacroAutomationClipboard
    >();
    if (!clipboard ||
        !clipboard->append(0, 0, control, address, true, false)) {
        return rejectClipboardStore(*this);
    }
    releaseOwnedPayloads(*this);
    clipboard->payloadKind = MacroClipboardPayloadKind::AUTOMATION;
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_AUTOMATION);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroDestination(
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
    clipboard->entries[0] = {
        .valid = true,
        .sourcePage = address.page,
        .sourceMacro = address.macro,
        .state = {},
    };
    clipboard->count = 1;
    clipboard->valid = true;
    clipboard->payloadKind = MacroClipboardPayloadKind::DESTINATION;
    clipboard->sourceTrack = address.track;
    clipboard->sourcePage = address.page;
    clipboard->sourceMacro = address.macro;
    clipboard->sourceMacroActive = true;
    clipboard->sourceCc = page.cc[address.macro];

    releaseOwnedPayloads(*this);
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_DESTINATION);
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

    core::state::modulation::ProjectControlMacroSlotView view{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            view
        )) {
        return rejectClipboardStore(*this);
    }
    clipboard->sourceSlotPresent = view.present;
    if (!clipboard->append(address.page, address.macro, pages.control, address)) {
        return rejectClipboardStore(*this);
    }

    releaseOwnedPayloads(*this);
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_SLOT);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroModulation(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    core::state::modulation::ProjectControlMacroSlotView view{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            control,
            address,
            view
        ) || !view.modulationStored || view.compatibilityMutationAmbiguous ||
        !view.primaryRecordedShape) {
        return rejectClipboardStore(*this);
    }
    auto clipboard = core::app::makeExtmemUnique<
        core::state::MacroAutomationClipboard
    >();
    if (!clipboard) return rejectClipboardStore(*this);
    clipboard->payloadKind = MacroClipboardPayloadKind::MODULATION;
    clipboard->sourceTrack = address.track;
    clipboard->sourcePage = address.page;
    clipboard->sourceMacro = address.macro;
    clipboard->sourceSlotPresent = true;
    if (!clipboard->append(
            address.page,
            address.macro,
            control,
            address,
            false,
            true
        )) {
        return rejectClipboardStore(*this);
    }
    releaseOwnedPayloads(*this);
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_MODULATION);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroModulationAssignment(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId
) {
    using namespace core::state::modulation;
    if (!core::state::macro::macroAutomationAddressValid(address)) {
        return rejectClipboardStore(*this);
    }
    const auto* binding = findProjectModulationBinding(
        control.authored.modulation,
        bindingId
    );
    if (binding == nullptr ||
        binding->destination != projectControlDestination(address)) {
        return rejectClipboardStore(*this);
    }
    const auto* source = findProjectModulator(
        control.authored.modulation,
        binding->sourceId
    );
    if (source == nullptr) return rejectClipboardStore(*this);

    auto payload = core::app::makeExtmemUnique<
        core::state::MacroModulationAssignmentClipboard
    >();
    if (!payload) return rejectClipboardStore(*this);
    payload->valid = true;
    payload->sourceId = source->id;
    payload->binding = *binding;
    payload->sourceName = source->name;

    releaseOwnedPayloads(*this);
    macroModulationAssignment = std::move(payload);
    commitClipboardKind(
        *this,
        StructureClipboardKind::MACRO_MODULATION_ASSIGNMENT
    );
    return true;
}

FLASHMEM bool StructureClipboardState::storeProjectModulatorSource(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulatorId sourceId
) {
    const auto* source = core::state::modulation::findProjectModulator(
        control.authored.modulation,
        sourceId
    );
    if (!source) return rejectClipboardStore(*this);
    releaseOwnedPayloads(*this);
    projectModulatorSource = {
        .valid = true,
        .sourceId = source->id,
        .kind = source->kind,
        .sourceName = source->name,
    };
    commitClipboardKind(
        *this,
        StructureClipboardKind::PROJECT_MODULATOR_SOURCE
    );
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
