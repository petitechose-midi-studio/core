#include "handler/macro/MacroAutomationClipboardOps.hpp"

#include <cmath>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::handler::macro::automation_clipboard_ops {

namespace {

FLASHMEM bool curvePayloadValid(
    const core::state::modulation::ProjectControlCurvePayload& curve,
    const core::state::MacroControlClipboardPointPool& pool
) {
    if (!curve.stored()) return curve.pointCount == 0U;
    if (curve.pointOffset >= pool.used ||
        static_cast<uint32_t>(curve.pointOffset) + curve.pointCount >
            pool.used) {
        return false;
    }
    return core::state::modulation::validProjectCurveSpec(
        curve.spec,
        pool.points.data() + curve.pointOffset,
        curve.pointCount
    );
}

FLASHMEM bool slotPayloadValid(
    const core::state::MacroAutomationClipboard& payload,
    uint8_t clipboardIndex,
    bool requireSingle = true
) {
    if (!payload.valid || payload.count == 0U ||
        (requireSingle && payload.count != 1U) ||
        clipboardIndex >= payload.count ||
        !payload.entries[clipboardIndex].valid) {
        return false;
    }
    const auto& entry = payload.entries[clipboardIndex];
    const auto& control = entry.control;
    const bool contiguous =
        !control.automation.stored() ||
        !control.recordedShape.stored() ||
        control.recordedShape.pointOffset ==
            static_cast<uint16_t>(
                control.automation.pointOffset +
                control.automation.pointCount
            );
    return (!control.automation.stored() ||
            control.automation.spec.valueDomain ==
                core::state::modulation::ProjectCurveValueDomain::
                    ABSOLUTE_UNIPOLAR) &&
           (!control.recordedShape.stored() ||
            (control.recordedShape.spec.valueDomain ==
                 core::state::modulation::ProjectCurveValueDomain::BIPOLAR &&
             std::isfinite(control.modulationAmount))) &&
           curvePayloadValid(control.automation, payload.pointPool) &&
           curvePayloadValid(control.recordedShape, payload.pointPool) &&
           contiguous &&
           (entry.destinationScaleQ15 ==
                 core::state::modulation::
                     PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 ||
            control.recordedShape.stored());
}

FLASHMEM bool slotPayloadValid(
    const core::state::MacroAutomationClipboard& payload
) {
    return slotPayloadValid(payload, 0U, true);
}

FLASHMEM const core::state::modulation::ProjectPackedCurvePoint*
entryPoints(
    const core::state::MacroAutomationClipboard& payload,
    const core::state::MacroAutomationClipboardEntry& entry
) {
    if (entry.control.automation.stored()) {
        return payload.pointPool.points.data() +
            entry.control.automation.pointOffset;
    }
    if (entry.control.recordedShape.stored()) {
        return payload.pointPool.points.data() +
            entry.control.recordedShape.pointOffset;
    }
    return nullptr;
}

FLASHMEM bool applyDestinationScale(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    uint16_t scaleQ15
) {
    using namespace core::state::modulation;
    if (scaleQ15 == PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) return true;
    const auto destination = projectControlDestination(address);
    const auto applied = setProjectModulationDestinationScale(
        control.authored.modulation,
        destination,
        scaleQ15
    );
    return applied.changed() ||
           projectModulationDestinationScaleQ15(
               control.authored.modulation,
               destination
           ) == scaleQ15;
}

FLASHMEM MacroTypedPastePreflight preflightCapacity(
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
    return entry.valid && entry.control.automation.stored();
}

FLASHMEM bool copySlotAutomationToClipboard(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            control,
            address,
            slot
        ) || !slot.automation.stored()) {
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
    const auto& curve = entry.control.automation;
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
        !payload.entries[0].control.automation.stored()) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    core::state::modulation::ProjectControlMacroDestinationView target{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            target
        )) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    const uint16_t required =
        payload.entries[0].control.automation.pointCount;
    const uint16_t reclaimable = target.automation.stored()
        ? target.automation.pointCount
        : 0U;
    const bool populated = target.automation.stored();
    return preflightCapacity(
        pages,
        required,
        reclaimable,
        target.automation.stored() ? 0U : 1U,
        0,
        0,
        target.automation.stored() ? 0U : 1U,
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
    const auto& curve = payload.entries[0].control.automation;
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
    return preflightSlotPasteEntry(pages, address, clipboard, 0U);
}

FLASHMEM MacroTypedPastePreflight preflightSlotPasteEntry(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    uint8_t clipboardIndex
) {
    MacroTypedPastePreflight rejected{};
    if (!core::state::macro::macroAutomationAddressValid(address)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    const bool singlePayload = clipboard.hasMacroSlot();
    const bool selectionPayload = clipboard.hasMacroSlotSelection();
    if ((!singlePayload && !selectionPayload) ||
        !clipboard.macroAutomationSet) {
        rejected.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return rejected;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(
            payload,
            clipboardIndex,
            singlePayload
        )) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    core::state::modulation::ProjectControlMacroDestinationView target{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            target
        ) || target.mutationAmbiguous() ||
        (target.primaryModulation.present() &&
         !target.primaryModulation.isRecordedShape())) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    const auto& entry = payload.entries[clipboardIndex];
    const bool sourceSlotPresent = singlePayload
        ? payload.sourceSlotPresent
        : entry.sourceSlotPresent;
    const auto& source = entry.control;
    const uint16_t required = sourceSlotPresent
        ? static_cast<uint16_t>(
              source.automation.pointCount + source.recordedShape.pointCount
          )
        : 0;
    // Replacing Automation releases its owned curve. A Project Modulator
    // source remains in the registry when its destination edge is removed.
    const uint16_t reclaimable = target.automation.pointCount;
    const auto& targetPage = pages.pageData(address.track, address.page);
    const bool populated =
        targetPage.isMacroActive(address.macro) || target.present();
    const bool wantsAutomation = sourceSlotPresent &&
        source.automation.stored();
    const bool wantsModulation = sourceSlotPresent &&
        source.recordedShape.stored();
    return preflightCapacity(
        pages,
        required,
        reclaimable,
        wantsAutomation && !target.automation.stored() ? 1U : 0U,
        wantsModulation ? 1U : 0U,
        wantsModulation && !target.primaryModulation.present() ? 1U : 0U,
        static_cast<uint16_t>(
            (wantsAutomation && !target.automation.stored() ? 1U : 0U) +
            (wantsModulation ? 1U : 0U)
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
    return pasteSlotEntryFromClipboard(
        pages,
        address,
        clipboard,
        0U,
        overwriteConfirmed
    );
}

FLASHMEM bool pasteSlotEntryFromClipboardInDomain(
    core::state::modulation::ProjectControlDomainState& domain,
    core::state::macro::MacroPageData& page,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    uint8_t clipboardIndex
) {
    const bool singlePayload = clipboard.hasMacroSlot();
    const bool selectionPayload = clipboard.hasMacroSlotSelection();
    if ((!singlePayload && !selectionPayload) ||
        !clipboard.macroAutomationSet ||
        !slotPayloadValid(
            *clipboard.macroAutomationSet,
            clipboardIndex,
            singlePayload
        )) {
        return false;
    }

    const auto& payload = *clipboard.macroAutomationSet;
    const auto& entry = payload.entries[clipboardIndex];
    const bool sourceSlotPresent = singlePayload
        ? payload.sourceSlotPresent
        : entry.sourceSlotPresent;
    const bool sourceMacroActive = singlePayload
        ? payload.sourceMacroActive
        : entry.sourceMacroActive;
    const uint8_t sourceCc = singlePayload
        ? payload.sourceCc
        : entry.sourceCc;
    const float sourceStaticValue = singlePayload
        ? payload.sourceStaticValue
        : entry.sourceStaticValue;
    const core::state::modulation::
        ProjectControlMacroDestinationPayload empty{};
    const auto& source = sourceSlotPresent ? entry.control : empty;
    const uint16_t pointCount = sourceSlotPresent
        ? static_cast<uint16_t>(
              source.automation.pointCount +
              source.recordedShape.pointCount
          )
        : 0U;
    if (!core::state::modulation::
            replaceProjectControlMacroDestinationInDomain(
                domain,
                address,
                source,
                entryPoints(payload, entry),
                pointCount
            )) {
        return false;
    }

    const auto destination =
        core::state::modulation::projectControlDestination(address);
    if (source.recordedShape.stored()) {
        const auto applied =
            core::state::modulation::
                setProjectModulationDestinationScale(
                    domain.modulation,
                    destination,
                    entry.destinationScaleQ15
                );
        if (!applied.changed() &&
            applied.status !=
                core::state::modulation::
                    ProjectModulationStatus::NO_CHANGE) {
            return false;
        }
    }
    if (core::state::modulation::
            projectModulationDestinationScaleQ15(
                domain.modulation,
                destination
            ) != entry.destinationScaleQ15) {
        return false;
    }

    page.setMacroActive(address.macro, sourceMacroActive);
    page.cc[address.macro] = sourceCc;
    page.values[address.macro] =
        core::state::macro::macroAutomationClamp01(
            sourceStaticValue
        );
    return true;
}

FLASHMEM bool pasteSlotEntryFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    uint8_t clipboardIndex,
    bool overwriteConfirmed
) {
    const auto plan = preflightSlotPasteEntry(
        pages,
        address,
        clipboard,
        clipboardIndex
    );
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    auto pendingDomain = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    if (!pendingDomain) return false;
    *pendingDomain = pages.control.authored;
    auto pendingPage = pages.pageData(address.track, address.page);
    if (!pasteSlotEntryFromClipboardInDomain(
            *pendingDomain,
            pendingPage,
            address,
            clipboard,
            clipboardIndex
        ) ||
        !core::state::modulation::validProjectModulationDomain(
            pendingDomain->modulation,
            pendingDomain->curves,
            &pendingDomain->automation
        )) {
        return false;
    }
    pages.control.authored = *pendingDomain;
    pages.control.markAuthoredMutation();
    pages.pageData(address.track, address.page) = pendingPage;
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
        !payload.entries[0].control.recordedShape.stored()) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    core::state::modulation::ProjectControlMacroDestinationView target{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            target
        ) || target.mutationAmbiguous() ||
        (target.primaryModulation.present() &&
         !target.primaryModulation.isRecordedShape())) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    const uint16_t required =
        payload.entries[0].control.recordedShape.pointCount;
    const bool populated = target.primaryModulation.present();
    return preflightCapacity(
        pages,
        required,
        0U,
        0,
        1U,
        target.primaryModulation.present() ? 0U : 1U,
        1U,
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
    const auto& control = payload.entries[0].control;
    const auto& curve = control.recordedShape;
    if (!core::state::modulation::replaceProjectControlRecordedShape(
        pages.control,
        address,
        curve,
        control.modulationAmount,
        payload.pointPool.points.data() + curve.pointOffset,
        curve.pointCount
    )) {
        return false;
    }
    return applyDestinationScale(
        pages.control,
        address,
        payload.entries[0].destinationScaleQ15
    );
}

}  // namespace core::handler::macro::automation_clipboard_ops
