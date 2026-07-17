#include "handler/macro/MacroAutomationClipboardOps.hpp"

#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::handler::macro::automation_clipboard_ops {

namespace {

bool curvePayloadValid(
    const core::state::macro::MacroAutomationCurveRef& curve,
    const core::state::macro::MacroAutomationPointPool& pool
) {
    if (!curve.active) return curve.pointCount == 0;
    if (curve.pointCount == 0 || curve.pointOffset >= pool.used) return false;
    return static_cast<uint32_t>(curve.pointOffset) + curve.pointCount <= pool.used;
}

bool slotPayloadValid(const core::state::MacroAutomationClipboard& payload) {
    if (!payload.valid || payload.count != 1 || !payload.entries[0].valid) {
        return false;
    }
    const auto& state = payload.entries[0].state;
    return curvePayloadValid(state.automation, payload.pointPool) &&
           curvePayloadValid(state.modulation, payload.pointPool) &&
           core::state::macro::macroAutomationSlotStateValidForMutation(
               state,
               payload.pointPool
           );
}

MacroTypedPastePreflight preflightCapacity(
    const core::state::macro::MacroPagesState& pages,
    uint16_t required,
    uint16_t reclaimable,
    uint16_t addedAutomationEntries,
    uint16_t addedModulationSources,
    uint16_t addedModulationBindings,
    uint16_t addedCurveRecords,
    bool overwrite
) {
    MacroTypedPastePreflight plan{};
    plan.requiredPointCount = required;
    plan.reclaimablePointCount = reclaimable;
    plan.freePointCount = static_cast<uint16_t>(
        core::state::modulation::PROJECT_CURVE_POINT_CAPACITY -
        pages.control.authored.curves.pointCount
    );
    const auto& authored = pages.control.authored;
    if (static_cast<uint32_t>(authored.automation.entryCount) +
            addedAutomationEntries >
            core::state::modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY ||
        static_cast<uint32_t>(authored.modulation.sourceCount) +
            addedModulationSources >
            core::state::modulation::PROJECT_MODULATOR_CAPACITY ||
        static_cast<uint32_t>(authored.modulation.outputBindingCount) +
            addedModulationBindings >
            core::state::modulation::PROJECT_MODULATION_BINDING_CAPACITY ||
        static_cast<uint32_t>(authored.curves.recordCount) + addedCurveRecords >
            core::state::modulation::PROJECT_CURVE_LIVE_CAPACITY) {
        plan.status = MacroTypedPasteStatus::SLOT_CAPACITY_EXHAUSTED;
        return plan;
    }
    if (static_cast<uint32_t>(required) >
        static_cast<uint32_t>(plan.freePointCount) + reclaimable) {
        plan.status = MacroTypedPasteStatus::POINT_POOL_EXHAUSTED;
        return plan;
    }
    plan.status = overwrite ? MacroTypedPasteStatus::OVERWRITE_REQUIRED
                            : MacroTypedPasteStatus::READY_FREE;
    return plan;
}

}  // namespace

FLASHMEM bool hasFirstClipboardAutomation(
    const core::state::StructureClipboardState& clipboard
) {
    if (!clipboard.hasMacroAutomation() ||
        !clipboard.macroAutomationSet ||
        !clipboard.macroAutomationSet->valid ||
        clipboard.macroAutomationSet->count == 0) {
        return false;
    }

    const auto& entry = clipboard.macroAutomationSet->entries[0];
    return entry.valid && entry.state.automation.active;
}

FLASHMEM bool copySlotAutomationToClipboard(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    core::state::modulation::ProjectControlMacroSlotView slot{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            control,
            address,
            slot
        ) || !slot.automationStored) {
        return false;
    }

    if (!clipboard.storeMacroAutomation(control, address) ||
        !clipboard.macroAutomationSet) {
        return false;
    }
    clipboard.macroAutomationSet->sourceTrack = address.track;
    clipboard.macroAutomationSet->sourcePage = address.page;
    clipboard.macroAutomationSet->sourceMacro = address.macro;
    clipboard.macroAutomationSet->sourceMacroActive = true;
    clipboard.macroAutomationSet->sourceSlotPresent = true;
    return true;
}

FLASHMEM bool pasteFirstClipboardAutomationToSlot(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    if (!hasFirstClipboardAutomation(clipboard)) return false;

    const auto& entry = clipboard.macroAutomationSet->entries[0];
    const auto& curve = entry.state.automation;
    return core::state::modulation::replaceProjectControlAutomation(
        control,
        address,
        curve,
        clipboard.macroAutomationSet->pointPool.points.data() +
            curve.pointOffset,
        curve.pointCount
    );
}

FLASHMEM MacroTypedPastePreflight preflightAutomationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight rejected{};
    if (!core::state::macro::macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(address.macro)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    if (!clipboard.hasMacroAutomation() || !clipboard.macroAutomationSet) {
        rejected.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return rejected;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload) ||
        !core::state::macro::macroCurveStored(payload.entries[0].state.automation)) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    core::state::modulation::ProjectControlMacroSlotView target{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            target
        )) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    const uint16_t required = payload.entries[0].state.automation.pointCount;
    const uint16_t reclaimable = target.automationStored
        ? target.legacy.automation.pointCount
        : 0U;
    const bool populated = target.automationStored;
    return preflightCapacity(
        pages,
        required,
        reclaimable,
        target.automationStored ? 0U : 1U,
        0,
        0,
        target.automationStored ? 0U : 1U,
        populated
    );
}

FLASHMEM bool pasteAutomationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightAutomationPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    const auto& curve = payload.entries[0].state.automation;
    return core::state::modulation::replaceProjectControlAutomation(
        pages.control,
        address,
        curve,
        payload.pointPool.points.data() + curve.pointOffset,
        curve.pointCount
    );
}

FLASHMEM bool copyDestinationToClipboard(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    return clipboard.storeMacroDestination(pages, address);
}

FLASHMEM MacroTypedPastePreflight preflightDestinationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight plan{};
    if (!core::state::macro::macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(address.macro)) {
        plan.status = MacroTypedPasteStatus::INVALID_TARGET;
        return plan;
    }
    if (!clipboard.hasMacroDestination() || !clipboard.macroAutomationSet) {
        plan.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return plan;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload) || payload.sourceCc > 127) {
        plan.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return plan;
    }
    if (pages.pageData(address.track, address.page).cc[address.macro] ==
        payload.sourceCc) {
        plan.status = MacroTypedPasteStatus::INVALID_TARGET;
        return plan;
    }
    plan.status = MacroTypedPasteStatus::OVERWRITE_REQUIRED;
    return plan;
}

FLASHMEM bool pasteDestinationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightDestinationPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    auto& page = pages.pageData(address.track, address.page);
    page.cc[address.macro] = clipboard.macroAutomationSet->sourceCc;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool copySlotToClipboard(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    return clipboard.storeMacroSlot(pages, address);
}

FLASHMEM MacroTypedPastePreflight preflightSlotPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight rejected{};
    if (!core::state::macro::macroAutomationAddressValid(address)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    if (!clipboard.hasMacroSlot() || !clipboard.macroAutomationSet) {
        rejected.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return rejected;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload)) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    core::state::modulation::ProjectControlMacroSlotView target{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            target
        ) || target.legacyMutationAmbiguous ||
        (target.modulationStored && !target.primaryRecordedShape)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    const uint16_t required = payload.sourceSlotPresent
        ? core::state::macro::macroAutomationStoredPointCount(
              payload.entries[0].state,
              payload.pointPool
          )
        : 0;
    const uint16_t reclaimable = static_cast<uint16_t>(
        target.legacy.automation.pointCount +
        target.legacy.modulation.pointCount
    );
    const auto& targetPage = pages.pageData(address.track, address.page);
    const bool populated = targetPage.isMacroActive(address.macro) || target.present;
    const auto& source = payload.entries[0].state;
    const bool wantsAutomation = payload.sourceSlotPresent &&
        core::state::macro::macroCurveStored(source.automation);
    const bool wantsModulation = payload.sourceSlotPresent &&
        core::state::macro::macroCurveStored(source.modulation);
    return preflightCapacity(
        pages,
        required,
        reclaimable,
        wantsAutomation && !target.automationStored ? 1U : 0U,
        wantsModulation && !target.modulationStored ? 1U : 0U,
        wantsModulation && !target.modulationStored ? 1U : 0U,
        static_cast<uint16_t>(
            (wantsAutomation && !target.automationStored ? 1U : 0U) +
            (wantsModulation && !target.modulationStored ? 1U : 0U)
        ),
        populated
    );
}

FLASHMEM bool pasteSlotFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightSlotPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    const core::state::macro::MacroAutomationSlotState empty{};
    const auto& source = payload.sourceSlotPresent
        ? payload.entries[0].state
        : empty;
    const uint16_t pointCount = payload.sourceSlotPresent
        ? core::state::macro::macroAutomationStoredPointCount(
              source,
              payload.pointPool
          )
        : 0U;
    if (!core::state::modulation::replaceProjectControlMacroSlot(
            pages.control,
            address,
            source,
            pointCount > 0U ? payload.pointPool.points.data() : nullptr,
            pointCount
        )) {
        return false;
    }

    auto& page = pages.pageData(address.track, address.page);
    page.setMacroActive(address.macro, payload.sourceMacroActive);
    page.cc[address.macro] = payload.sourceCc;
    page.values[address.macro] = core::state::macro::macroAutomationClamp01(
        payload.sourceStaticValue
    );
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool copyModulationToClipboard(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    return clipboard.storeMacroModulation(control, address);
}

FLASHMEM bool copyModulationAssignmentToClipboard(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId,
    core::state::StructureClipboardState& clipboard
) {
    return clipboard.storeMacroModulationAssignment(
        control,
        address,
        bindingId
    );
}

FLASHMEM bool modulationAssignmentDraftFromClipboard(
    const core::state::StructureClipboardState& clipboard,
    const core::state::modulation::ModulationDestination& destination,
    core::state::modulation::ModulationBindingDraft& out
) {
    using namespace core::state::modulation;
    if (!clipboard.hasMacroModulationAssignment() ||
        !clipboard.macroModulationAssignment ||
        !modulationDestinationValid(destination)) {
        return false;
    }
    const auto& payload = *clipboard.macroModulationAssignment;
    const auto& binding = payload.binding;
    if (!payload.valid || binding.sourceId != payload.sourceId ||
        binding.amountQ15 == std::numeric_limits<int16_t>::min() ||
        static_cast<uint8_t>(binding.application) >
            static_cast<uint8_t>(ModulationApplication::FROM_BASE) ||
        binding.transfer != ModulationTransfer::LINEAR) {
        return false;
    }
    out = {
        .sourceId = payload.sourceId,
        .destination = destination,
        .amountQ15 = binding.amountQ15,
        .application = binding.application,
        .transfer = binding.transfer,
        .slewMs = binding.slewMs,
        .enabled = (binding.flags &
                    PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U,
    };
    return true;
}

FLASHMEM MacroTypedPastePreflight preflightModulationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight rejected{};
    if (!core::state::macro::macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(address.macro)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    if (clipboard.hasMacroModulationAssignment()) {
        using namespace core::state::modulation;
        ModulationBindingDraft draft{};
        if (!modulationAssignmentDraftFromClipboard(
                clipboard,
                projectControlDestination(address),
                draft
            )) {
            rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
            return rejected;
        }
        const auto& graph = pages.control.authored.modulation;
        const auto* source = findProjectModulator(graph, draft.sourceId);
        if (source == nullptr) {
            rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
            return rejected;
        }
        if (!modulatorReachContains(source->reach, draft.destination)) {
            rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
            return rejected;
        }
        bool duplicate = false;
        for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
            const auto& existing = graph.outputBindings[index];
            if (existing.sourceId == draft.sourceId &&
                existing.destination == draft.destination) {
                duplicate = true;
                break;
            }
        }
        return preflightCapacity(
            pages,
            0U,
            0U,
            0U,
            0U,
            duplicate ? 0U : 1U,
            0U,
            duplicate
        );
    }
    if (!clipboard.hasMacroModulation() || !clipboard.macroAutomationSet) {
        rejected.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return rejected;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload) ||
        !core::state::macro::macroCurveStored(payload.entries[0].state.modulation)) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    core::state::modulation::ProjectControlMacroSlotView target{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            target
        ) || target.legacyMutationAmbiguous ||
        (target.modulationStored && !target.primaryRecordedShape)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    const uint16_t required = payload.entries[0].state.modulation.pointCount;
    const uint16_t reclaimable = target.modulationStored
        ? target.legacy.modulation.pointCount
        : 0U;
    const bool populated = target.modulationStored;
    return preflightCapacity(
        pages,
        required,
        reclaimable,
        0,
        target.modulationStored ? 0U : 1U,
        target.modulationStored ? 0U : 1U,
        target.modulationStored ? 0U : 1U,
        populated
    );
}

FLASHMEM bool pasteModulationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightModulationPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    if (clipboard.hasMacroModulationAssignment()) {
        using namespace core::state::modulation;
        ModulationBindingDraft draft{};
        if (!modulationAssignmentDraftFromClipboard(
                clipboard,
                projectControlDestination(address),
                draft
            )) {
            return false;
        }
        auto& graph = pages.control.authored.modulation;
        for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
            auto& existing = graph.outputBindings[index];
            if (existing.sourceId != draft.sourceId ||
                existing.destination != draft.destination) {
                continue;
            }
            if (!overwriteConfirmed ||
                !updateProjectModulationBinding(
                    graph,
                    existing.id,
                    draft.amountQ15,
                    draft.application,
                    draft.transfer,
                    draft.enabled,
                    draft.slewMs
                ).changed()) {
                return false;
            }
            pages.control.markAuthoredMutation();
            return true;
        }
        if (overwriteConfirmed || !addProjectModulationBinding(graph, draft).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
        return true;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    const auto& state = payload.entries[0].state;
    const auto& curve = state.modulation;
    return core::state::modulation::replaceProjectControlModulation(
        pages.control,
        address,
        curve,
        state.modulationDepth,
        payload.pointPool.points.data() + curve.pointOffset,
        curve.pointCount
    );
}

}  // namespace core::handler::macro::automation_clipboard_ops
