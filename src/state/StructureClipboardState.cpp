#include "state/StructureClipboardState.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/macro/MacroAutomationDomain.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state {

FLASHMEM StructureClipboardState::~StructureClipboardState() = default;

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
    drumContext = false;
    count = 0;
    span = 0;
    entries = {};
}

FLASHMEM void SequencerPageSelectionClipboard::reset() {
    valid = false;
    sourceFirstPage =
        core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    count = 0;
    pages = {};
}

FLASHMEM void SequencerTrackSelectionClipboard::reset() {
    valid = false;
    count = 0;
    projectControl.reset();
    for (auto& track : tracks) {
        track = {};
    }
}

FLASHMEM void MacroPageSelectionClipboard::reset() {
    valid = false;
    sourceTrack = core::state::macro::TRACK_COUNT;
    sourceFirstPage = core::state::macro::PAGE_COUNT;
    count = 0U;
    pages = {};
    projectControl.reset();
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
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            control,
            address,
            view
        ) || view.mutationAmbiguous() ||
        (view.primaryModulation.present() &&
         !view.primaryModulation.isRecordedShape())) {
        return false;
    }

    core::state::modulation::ProjectControlMacroDestinationPayload copied{};
    const uint16_t automationCount =
        includeAutomation && view.automation.stored()
        ? view.automation.pointCount
        : 0U;
    const uint16_t modulationCount =
        includeModulation && view.primaryModulation.isRecordedShape()
        ? view.primaryModulation.recordedShape.pointCount
        : 0U;
    const uint32_t required = static_cast<uint32_t>(automationCount) +
                              modulationCount;
    if (required > static_cast<uint32_t>(
            core::state::modulation::PROJECT_CURVE_POINT_CAPACITY -
            pointPool.used
        )) {
        return false;
    }

    const uint16_t start = pointPool.used;
    uint16_t cursor = start;
    if (automationCount > 0U) {
        for (uint16_t index = 0; index < automationCount; ++index) {
            const auto& source = control.authored.curves.points[
                view.automation.pointOffset + index
            ];
            pointPool.points[cursor + index] = source;
        }
        copied.automation = {
            .spec = view.automation.spec,
            .pointOffset = cursor,
            .pointCount = automationCount,
            .enabled = view.automation.enabled,
        };
        cursor = static_cast<uint16_t>(cursor + automationCount);
    }
    if (modulationCount > 0U) {
        for (uint16_t index = 0; index < modulationCount; ++index) {
            const auto& source = control.authored.curves.points[
                view.primaryModulation.recordedShape.pointOffset + index
            ];
            pointPool.points[cursor + index] = source;
        }
        copied.recordedShape = {
            .spec = view.primaryModulation.recordedShape.spec,
            .pointOffset = cursor,
            .pointCount = modulationCount,
            .enabled = view.primaryModulation.recordedShape.enabled,
        };
        copied.modulationAmount = view.primaryModulation.amount;
        cursor = static_cast<uint16_t>(cursor + modulationCount);
    }
    pointPool.used = cursor;
    entries[count] = MacroAutomationClipboardEntry{
        .valid = true,
        .sourcePage = entrySourcePage,
        .sourceMacro = entrySourceMacro,
        .control = copied,
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
    clipboard.sequencerGraph.reset();
    clipboard.sequencerCcLanes.reset();
    clipboard.sequencerDrumTrack.reset();
    clipboard.sequencerDrumLaneSelectionMask = 0U;
    clipboard.sequencerDrumLaneSelectionCount = 0U;
    clipboard.sequencerTrackSelection.reset();
    clipboard.macroPageSelection.reset();
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
            core::state::modulation::ProjectControlMacroDestinationView view{};
            if (!core::state::modulation::readProjectControlMacroDestination(
                    control,
                    address,
                    view
                )) {
                return {.success = false};
            }
            if (!view.present()) continue;
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

FLASHMEM bool StructureClipboardState::hasMacroAutomation() const {
    return kind.get() == StructureClipboardKind::MACRO_AUTOMATION &&
           macroAutomationSet && macroAutomationSet->valid &&
           macroAutomationSet->count > 0U &&
           macroAutomationSet->payloadKind ==
               MacroClipboardPayloadKind::AUTOMATION &&
           macroAutomationSet->entries[0].control.automation.stored();
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
    if (!automationSet.success) return false;
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
    if (!automationSet.success) return false;
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
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            control,
            address,
            view
        ) || !view.automation.stored()) {
        return false;
    }
    auto clipboard = core::app::makeExtmemUnique<
        core::state::MacroAutomationClipboard
    >();
    if (!clipboard ||
        !clipboard->append(0, 0, control, address, true, false)) {
        return false;
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
        return false;
    }
    const auto& page = pages.pageData(address.track, address.page);
    if (!page.isMacroActive(address.macro)) {
        return false;
    }

    auto clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
    if (!clipboard) return false;
    clipboard->entries[0] = {
        .valid = true,
        .sourcePage = address.page,
        .sourceMacro = address.macro,
        .control = {},
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
        return false;
    }

    const auto& page = pages.pageData(address.track, address.page);
    if (!page.isMacroActive(address.macro)) {
        return false;
    }

    auto clipboard = core::app::makeExtmemUnique<core::state::MacroAutomationClipboard>();
    if (!clipboard) return false;
    clipboard->payloadKind = MacroClipboardPayloadKind::SLOT;
    clipboard->sourceTrack = address.track;
    clipboard->sourcePage = address.page;
    clipboard->sourceMacro = address.macro;
    clipboard->sourceMacroActive = true;
    clipboard->sourceCc = page.cc[address.macro];
    clipboard->sourceStaticValue = page.values[address.macro];

    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            view
        )) {
        return false;
    }
    clipboard->sourceSlotPresent = view.present();
    if (!clipboard->append(address.page, address.macro, pages.control, address)) {
        return false;
    }
    auto& entry = clipboard->entries[0];
    entry.sourceMacroActive = true;
    entry.sourceSlotPresent = clipboard->sourceSlotPresent;
    entry.sourceCc = clipboard->sourceCc;
    entry.sourceStaticValue = clipboard->sourceStaticValue;

    releaseOwnedPayloads(*this);
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(*this, StructureClipboardKind::MACRO_SLOT);
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroSlotSelection(
    const core::state::macro::MacroPagesState& pages,
    uint8_t sourceTrack,
    const oc::note::sequencer::StepBitMask128& selectedMask
) {
    if (sourceTrack >= core::state::macro::TRACK_COUNT) {
        return false;
    }

    auto clipboard = core::app::makeExtmemUnique<
        core::state::MacroAutomationClipboard
    >();
    if (!clipboard) return false;
    clipboard->payloadKind = MacroClipboardPayloadKind::SLOT;
    clipboard->sourceTrack = sourceTrack;

    bool first = true;
    for (uint8_t linear = 0U;
         linear < core::state::macro::PAGE_COUNT *
                      core::state::macro::MACRO_COUNT;
         ++linear) {
        if (!selectedMask.test(linear)) continue;
        const uint8_t page = static_cast<uint8_t>(
            linear / core::state::macro::MACRO_COUNT
        );
        const uint8_t macro = static_cast<uint8_t>(
            linear % core::state::macro::MACRO_COUNT
        );
        const auto& pageData = pages.pageData(sourceTrack, page);
        if (!pages.tracks[sourceTrack].isPageEnabled(page) ||
            !pageData.isMacroActive(macro)) {
            return false;
        }
        const core::state::macro::MacroAutomationSlotAddress address{
            .track = sourceTrack,
            .page = page,
            .macro = macro,
        };
        core::state::modulation::ProjectControlMacroDestinationView view{};
        if (!core::state::modulation::readProjectControlMacroDestination(
                pages.control,
                address,
                view
            )) {
            return false;
        }
        const uint8_t entryIndex = clipboard->count;
        if (!clipboard->append(
                page,
                macro,
                pages.control,
                address
            )) {
            return false;
        }
        auto& entry = clipboard->entries[entryIndex];
        entry.sourceMacroActive = true;
        entry.sourceSlotPresent = view.present();
        entry.sourceCc = pageData.cc[macro];
        entry.sourceStaticValue = pageData.values[macro];

        if (first) {
            clipboard->sourcePage = page;
            clipboard->sourceMacro = macro;
            clipboard->sourceMacroActive = true;
            clipboard->sourceSlotPresent = entry.sourceSlotPresent;
            clipboard->sourceCc = entry.sourceCc;
            clipboard->sourceStaticValue = entry.sourceStaticValue;
            first = false;
        }
    }
    if (clipboard->count == 0U) {
        return false;
    }

    releaseOwnedPayloads(*this);
    macroAutomationSet = std::move(clipboard);
    commitClipboardKind(
        *this,
        StructureClipboardKind::MACRO_SLOT_SELECTION
    );
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroPageSelection(
    const core::state::macro::MacroPagesState& pages,
    uint8_t sourceTrack,
    uint16_t selectedMask
) {
    if (sourceTrack >= core::state::macro::TRACK_COUNT) {
        return false;
    }
    const uint16_t mask = static_cast<uint16_t>(
        selectedMask &
        pages.tracks[sourceTrack].enabledPageMask
    );
    if (mask == 0U) return false;

    auto clipboard = core::app::makeExtmemUnique<
        core::state::MacroPageSelectionClipboard
    >();
    if (!clipboard) return false;
    clipboard->projectControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >(pages.control.authored);
    if (!clipboard->projectControl) {
        return false;
    }
    clipboard->sourceTrack = sourceTrack;
    for (uint8_t page = 0U;
         page < core::state::macro::PAGE_COUNT;
         ++page) {
        const uint16_t bit = static_cast<uint16_t>(1U << page);
        if ((mask & bit) == 0U) continue;
        if (clipboard->count >= clipboard->pages.size()) {
            return false;
        }
        if (clipboard->sourceFirstPage >=
            core::state::macro::PAGE_COUNT) {
            clipboard->sourceFirstPage = page;
        }
        auto& entry = clipboard->pages[clipboard->count++];
        entry.valid = true;
        entry.sourcePage = page;
        entry.page = pages.pageData(sourceTrack, page);
    }
    clipboard->valid = clipboard->count > 0U;
    if (!clipboard->valid) return false;

    releaseOwnedPayloads(*this);
    macroPageSelection = std::move(clipboard);
    commitClipboardKind(
        *this,
        StructureClipboardKind::MACRO_PAGE_SELECTION
    );
    return true;
}

FLASHMEM bool StructureClipboardState::storeMacroModulation(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            control,
            address,
            view
        ) || !view.primaryModulation.isRecordedShape() ||
        view.mutationAmbiguous()) {
        return false;
    }
    auto clipboard = core::app::makeExtmemUnique<
        core::state::MacroAutomationClipboard
    >();
    if (!clipboard) return false;
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
        return false;
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
        return false;
    }
    const auto* binding = findProjectModulationBinding(
        control.authored.modulation,
        bindingId
    );
    if (binding == nullptr ||
        binding->destination != projectControlDestination(address)) {
        return false;
    }
    const auto* source = findProjectModulator(
        control.authored.modulation,
        binding->sourceId
    );
    if (source == nullptr) return false;

    auto payload = core::app::makeExtmemUnique<
        core::state::MacroModulationAssignmentClipboard
    >();
    if (!payload) return false;
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
    if (!source) return false;
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
        return false;
    }
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    if (!cloneSequencerGraph(graphCopy, graph)) {
        return false;
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
    const core::state::sequencer::SequencerCcLaneBank* ccLanes,
    const core::state::sequencer::DrumTrackState* drumTrack
) {
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    core::state::sequencer::SequencerCcLaneBankPtr ccLaneCopy;
    core::app::ExtmemUniquePtr<
        core::state::sequencer::DrumTrackState
    > drumTrackCopy;
    if (!cloneSequencerGraph(graphCopy, graph) ||
        !core::state::sequencer::cloneSequencerCcLaneBank(
            ccLaneCopy,
            ccLanes
        )) {
        return false;
    }
    if (drumTrack != nullptr) {
        drumTrackCopy = core::app::makeExtmemUnique<
            core::state::sequencer::DrumTrackState
        >(*drumTrack);
        if (!drumTrackCopy) return false;
    }

    releaseOwnedPayloads(*this);
    sequencerTrack = track;
    sequencerTrackSource = sourceTrack < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
        ? sourceTrack
        : core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    sequencerGraph = std::move(graphCopy);
    sequencerCcLanes = std::move(ccLaneCopy);
    sequencerDrumTrack = std::move(drumTrackCopy);
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
        return false;
    }
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    if (!cloneSequencerGraph(graphCopy, &graph)) {
        return false;
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
        return false;
    }
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graphCopy;
    if (!cloneSequencerGraph(graphCopy, graph)) {
        return false;
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
    if (!pages.valid || pages.count == 0U) {
        return false;
    }
    core::app::ExtmemUniquePtr<
        oc::note::sequencer::StepSequencerGraph
    > graphCopy;
    if (!cloneSequencerGraph(graphCopy, graph)) {
        return false;
    }

    releaseOwnedPayloads(*this);
    sequencerPageSelection = pages;
    sequencerGraph = std::move(graphCopy);
    commitClipboardKind(
        *this,
        StructureClipboardKind::SEQUENCER_PAGE_SELECTION
    );
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerDrumLaneSelection(
    const core::state::sequencer::DrumTrackState& source,
    uint16_t selectedMask,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    const uint8_t laneCount = source.kit.laneCount >
            core::state::sequencer::DRUM_MAX_LANES
        ? core::state::sequencer::DRUM_MAX_LANES
        : source.kit.laneCount;
    const uint16_t activeMask = laneCount >= 16U
        ? 0xFFFFU
        : static_cast<uint16_t>((uint16_t{1U} << laneCount) - 1U);
    selectedMask = static_cast<uint16_t>(selectedMask & activeMask);
    if (selectedMask == 0U) return false;

    auto content = core::app::makeExtmemUnique<
        core::state::sequencer::DrumTrackState
    >();
    if (!content) return false;
    content->reset(core::state::sequencer::DrumKitPreset::EMPTY);
    content->kit.laneCount = laneCount;

    uint8_t selectedCount = 0U;
    bool advancedContent = false;
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        if ((selectedMask & static_cast<uint16_t>(1U << lane)) == 0U) {
            continue;
        }
        ++selectedCount;
        content->pattern.lanes[lane] = source.pattern.lanes[lane];
        for (uint8_t step = 0U;
             step < core::state::sequencer::DRUM_MAX_STEPS;
             ++step) {
            const int16_t slot = source.advancedRootSlot(lane, step);
            if (slot < 0) continue;
            if (graph == nullptr ||
                !core::state::sequencer::inspectSequencerGraphPayload(
                    *graph,
                    core::state::sequencer::rootStepNodeId(
                        static_cast<uint8_t>(slot)
                    ),
                    0U
                ).ok()) {
                return false;
            }
            content->advancedStepKeys[static_cast<uint8_t>(slot)] =
                source.advancedStepKeys[static_cast<uint8_t>(slot)];
            advancedContent = true;
        }
    }

    core::app::ExtmemUniquePtr<
        oc::note::sequencer::StepSequencerGraph
    > graphCopy;
    if (advancedContent && !cloneSequencerGraph(graphCopy, graph)) {
        return false;
    }

    releaseOwnedPayloads(*this);
    sequencerDrumTrack = std::move(content);
    sequencerGraph = std::move(graphCopy);
    sequencerDrumLaneSelectionMask = selectedMask;
    sequencerDrumLaneSelectionCount = selectedCount;
    commitClipboardKind(
        *this,
        StructureClipboardKind::SEQUENCER_DRUM_LANE_SELECTION
    );
    return true;
}

FLASHMEM bool StructureClipboardState::storeSequencerTrackSelection(
    core::app::ExtmemUniquePtr<
        core::state::SequencerTrackSelectionClipboard
    > tracks
) {
    if (!tracks || !tracks->valid || tracks->count == 0U) {
        return false;
    }

    releaseOwnedPayloads(*this);
    sequencerTrackSelection = std::move(tracks);
    commitClipboardKind(
        *this,
        StructureClipboardKind::SEQUENCER_TRACK_SELECTION
    );
    return true;
}

}  // namespace core::state
