#include "state/macro/MacroHistoryInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
namespace core::state::macro {

using namespace history_detail;

FLASHMEM bool MacroHistoryService::setModulationDepthCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    float depth
) {
    const auto bindingId =
        core::state::modulation::projectControlFocusedModulationBinding(
            pages.control,
            address
        );
    return setModulationBindingDepthCoalesced(
        pages,
        address,
        bindingId,
        depth
    );
}

FLASHMEM bool MacroHistoryService::setMacroValueCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    float value
) {
    if (!macroAutomationAddressValid(address) || !std::isfinite(value) ||
        pendingModulatorSlot_() != nullptr) {
        return false;
    }
    auto& page = pages.pageData(address.track, address.page);
    if (!page.isMacroActive(address.macro)) return false;
    const float next = macroAutomationClamp01(value);
    if (sameFloatBits(page.values[address.macro], next)) return false;

    if (coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == MacroHistoryActionKind::STATIC_VALUE_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->valueEdit.valid &&
            sameFloatBits(page.values[address.macro], previous->valueEdit.after)) {
            page.values[address.macro] = next;
            previous->valueEdit.after = next;
            clearRedo_();
            return true;
        }
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::STATIC_VALUE_EDIT;
    change->address = address;
    change->valueEdit.before = page.values[address.macro];
    change->valueEdit.after = next;
    change->valueEdit.valid = true;

    page.values[address.macro] = next;
    recordNewEntry_(std::move(change));
    coalescing_ = true;
    coalesced_kind_ = MacroHistoryActionKind::STATIC_VALUE_EDIT;
    coalesced_address_ = address;
    return true;
}

FLASHMEM bool MacroHistoryService::setAutomationMetadataCoalesced_(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const core::state::modulation::ProjectCurveSpec& spec,
    MacroHistoryActionKind kind
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address) ||
        pendingModulatorSlot_() != nullptr ||
        (kind != MacroHistoryActionKind::AUTOMATION_DURATION_EDIT &&
         kind != MacroHistoryActionKind::AUTOMATION_WINDOW_EDIT)) {
        return false;
    }
    ProjectControlMacroDestinationView view{};
    if (!readProjectControlMacroDestination(pages.control, address, view) ||
        !view.automation.stored() ||
        sameCurveSpec(view.automation.spec, spec)) {
        return false;
    }

    if (coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == kind && sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->automation &&
            previous->automation->metadata.valid &&
            liveAutomationMetadataMatches(
                pages,
                address,
                previous->automation->metadata,
                true,
                false
            )) {
            const auto changed = setProjectAutomationCurveSpec(
                pages.control.authored.automation,
                pages.control.authored.curves,
                projectControlDestination(address),
                spec
            );
            if (!changed.changed()) return false;
            pages.control.markAuthoredMutation();
            previous->automation->metadata.after = spec;
            clearRedo_();
            return true;
        }
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->automation =
        core::app::makeExtmemUnique<MacroAutomationHistoryPayload>();
    if (!change->automation) return false;
    change->kind = kind;
    change->address = address;
    if (!captureAutomationMetadataHistory(
            pages,
            address,
            change->automation->metadata
        )) {
        return false;
    }
    const auto changed = setProjectAutomationCurveSpec(
        pages.control.authored.automation,
        pages.control.authored.curves,
        projectControlDestination(address),
        spec
    );
    if (!changed.changed()) return false;
    pages.control.markAuthoredMutation();
    change->automation->metadata.after = spec;
    if (!liveAutomationMetadataMatches(
            pages,
            address,
            change->automation->metadata,
            true,
            true
        )) {
        const auto restored = setProjectAutomationCurveSpec(
            pages.control.authored.automation,
            pages.control.authored.curves,
            projectControlDestination(address),
            change->automation->metadata.before
        );
        if (restored.changed()) pages.control.markAuthoredMutation();
        return false;
    }

    endCoalescing();
    recordNewEntry_(std::move(change));
    coalescing_ = true;
    coalesced_kind_ = kind;
    coalesced_address_ = address;
    return true;
}

FLASHMEM bool MacroHistoryService::setAutomationDurationBeatsCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    float durationBeats
) {
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            view
        ) || !view.automation.stored()) {
        return false;
    }
    auto spec = view.automation.spec;
    spec.durationTicks = macroAutomationTicksFromBeats(durationBeats);
    spec.windowOffsetTicks = static_cast<uint16_t>(
        spec.windowOffsetTicks %
        std::max<uint16_t>(spec.sourceDurationTicks, 1U)
    );
    return setAutomationMetadataCoalesced_(
        pages,
        address,
        spec,
        MacroHistoryActionKind::AUTOMATION_DURATION_EDIT
    );
}

FLASHMEM bool MacroHistoryService::setAutomationWindowOffsetBeatsCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    float offsetBeats
) {
    if (!std::isfinite(offsetBeats)) return false;
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            view
        ) || !view.automation.stored()) {
        return false;
    }
    const float safeBeats = std::max(offsetBeats, 0.0f);
    const uint32_t rawTicks = static_cast<uint32_t>(std::clamp<long long>(
        std::llround(
            safeBeats *
            static_cast<float>(
                core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT
            )
        ),
        0LL,
        static_cast<long long>(UINT16_MAX)
    ));
    auto spec = view.automation.spec;
    spec.windowOffsetTicks = static_cast<uint16_t>(
        rawTicks % std::max<uint16_t>(spec.sourceDurationTicks, 1U)
    );
    return setAutomationMetadataCoalesced_(
        pages,
        address,
        spec,
        MacroHistoryActionKind::AUTOMATION_WINDOW_EDIT
    );
}

FLASHMEM bool MacroHistoryService::setManualOverrideCoalesced(
    MacroPagesState& pages,
    MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    float value,
    bool coalesceValue
) {
    if (!macroAutomationAddressValid(address) || !std::isfinite(value) ||
        pendingModulatorSlot_() != nullptr) {
        return false;
    }
    auto& page = pages.pageData(address.track, address.page);
    if (!page.isMacroActive(address.macro)) return false;

    const float next = macroAutomationClamp01(value);
    const float beforeBase = page.values[address.macro];
    bool beforeActive = false;
    float beforeManualValue = 0.0f;
    readManualOverride(overrides, address, beforeActive, beforeManualValue);
    const bool baseChanged = !sameFloatBits(beforeBase, next);
    const bool manualChanged = !beforeActive ||
        !sameFloatBits(beforeManualValue, next);
    if (!baseChanged && !manualChanged) return false;

    const auto kind = coalesceValue
        ? MacroHistoryActionKind::STATIC_VALUE_EDIT
        : MacroHistoryActionKind::MANUAL_OVERRIDE_STATE;
    if (coalesceValue && coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == kind && sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->auxiliary &&
            previous->auxiliary->manualOverride.valid &&
            sameFloatBits(beforeBase, previous->valueEdit.after) &&
            manualOverrideMatches(
                overrides,
                address,
                previous->auxiliary->manualOverride.afterActive,
                previous->auxiliary->manualOverride.afterValue
            )) {
            const auto status = overrides.activate(address, next);
            if (status == MacroManualOverrideState::ActivateStatus::INVALID_ADDRESS ||
                status == MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED) {
                return false;
            }
            page.values[address.macro] = next;
            previous->valueEdit.after = next;
            previous->valueEdit.valid = previous->valueEdit.valid ||
                !sameFloatBits(previous->valueEdit.before, next);
            previous->auxiliary->manualOverride.afterActive = true;
            previous->auxiliary->manualOverride.afterValue = next;
            clearRedo_();
            return true;
        }
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->auxiliary = core::app::makeExtmemUnique<
        MacroAuxiliaryHistoryPayload
    >();
    if (!change->auxiliary) return false;
    change->kind = kind;
    change->address = address;
    change->valueEdit.before = beforeBase;
    change->valueEdit.after = next;
    change->valueEdit.valid = baseChanged;
    change->auxiliary->manualOverride.beforeActive = beforeActive;
    change->auxiliary->manualOverride.beforeValue = beforeManualValue;
    change->auxiliary->manualOverride.afterActive = true;
    change->auxiliary->manualOverride.afterValue = next;
    change->auxiliary->manualOverride.valid = true;

    const auto status = overrides.activate(address, next);
    if (status == MacroManualOverrideState::ActivateStatus::INVALID_ADDRESS ||
        status == MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED) {
        return false;
    }
    page.values[address.macro] = next;
    if (!coalesceValue) endCoalescing();
    recordNewEntry_(std::move(change));
    coalescing_ = coalesceValue;
    if (coalescing_) {
        coalesced_kind_ = kind;
        coalesced_address_ = address;
    }
    return true;
}

FLASHMEM bool MacroHistoryService::resumeManualOverride(
    MacroPagesState& pages,
    MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address
) {
    if (!macroAutomationAddressValid(address) ||
        pendingModulatorSlot_() != nullptr ||
        !pages.pageData(address.track, address.page).isMacroActive(address.macro)) {
        return false;
    }
    float beforeValue = 0.0f;
    if (!overrides.valueFor(address, beforeValue)) return false;

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->auxiliary = core::app::makeExtmemUnique<
        MacroAuxiliaryHistoryPayload
    >();
    if (!change->auxiliary) return false;
    change->kind = MacroHistoryActionKind::MANUAL_OVERRIDE_STATE;
    change->address = address;
    change->auxiliary->manualOverride.beforeActive = true;
    change->auxiliary->manualOverride.beforeValue = beforeValue;
    change->auxiliary->manualOverride.afterActive = false;
    change->auxiliary->manualOverride.afterValue = 0.0f;
    change->auxiliary->manualOverride.valid = true;

    if (!overrides.resume(address)) return false;
    endCoalescing();
    recordNewEntry_(std::move(change));
    return true;
}

FLASHMEM bool MacroHistoryService::setModulationBindingDepthCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId,
    float depth
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address) || !valid(bindingId) ||
        !std::isfinite(depth)) {
        return false;
    }
    auto* binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    const auto destination = projectControlDestination(address);
    if (binding == nullptr || binding->destination != destination) return false;
    const long rounded = std::lround(
        std::clamp(depth, -1.0f, 1.0f) * 32767.0f
    );
    const int16_t amountQ15 = static_cast<int16_t>(
        std::clamp<long>(rounded, -32767L, 32767L)
    );
    if (binding->amountQ15 == amountQ15) return false;

    if (coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == MacroHistoryActionKind::DEPTH_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->modulationAssignments &&
            liveModulationAssignmentsMatch(
                pages,
                previous->modulationAssignments->after
            )) {
            const auto beforeBinding = *binding;
            const bool enabled =
                (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
            if (!updateProjectModulationBinding(
                    pages.control.authored.modulation,
                    bindingId,
                    amountQ15,
                    binding->application,
                    binding->transfer,
                    enabled,
                    binding->slewMs
                ).changed()) {
                return false;
            }
            pages.control.markAuthoredBindingAmountMutation(
                bindingId
            );
            if (!captureModulationAssignments(
                    pages,
                    address,
                    previous->modulationAssignments->after
                )) {
                *binding = beforeBinding;
                pages.control.markAuthoredMutation();
                return false;
            }
            clearRedo_();
            return true;
        }
    }

    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::DEPTH_EDIT
    );
    if (!change) return false;
    binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    if (binding == nullptr) return false;
    const bool enabled =
        (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
    if (!updateProjectModulationBinding(
            pages.control.authored.modulation,
            bindingId,
            amountQ15,
            binding->application,
            binding->transfer,
            enabled,
            binding->slewMs
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredBindingAmountMutation(bindingId);
    return commitModulationAssignments_(pages, std::move(change), true);
}

FLASHMEM bool MacroHistoryService::setModulationDestinationScaleCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    uint16_t scaleQ15
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address)) return false;
    auto& graph = pages.control.authored.modulation;
    const auto destination = projectControlDestination(address);
    const uint16_t current = projectModulationDestinationScaleQ15(
        graph,
        destination
    );
    if (current == scaleQ15) return false;

    if (coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == MacroHistoryActionKind::GLOBAL_DEPTH_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->destinationScale.valid &&
            previous->destinationScale.destination == destination &&
            previous->destinationScale.afterScaleQ15 == current &&
            setProjectModulationDestinationScale(
                graph,
                destination,
                scaleQ15
            ).changed()) {
            pages.control.markAuthoredDestinationScaleMutation(
                destination
            );
            previous->destinationScale.afterScaleQ15 = scaleQ15;
            clearRedo_();
            return true;
        }
    }

    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return false;
    change->kind = MacroHistoryActionKind::GLOBAL_DEPTH_EDIT;
    change->address = address;
    change->destinationScale = {
        .destination = destination,
        .beforeScaleQ15 = current,
        .afterScaleQ15 = scaleQ15,
        .valid = true,
    };
    if (!setProjectModulationDestinationScale(
            graph,
            destination,
            scaleQ15
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredDestinationScaleMutation(
        destination
    );
    endCoalescing();
    recordNewEntry_(std::move(change));
    coalescing_ = true;
    coalesced_kind_ = MacroHistoryActionKind::GLOBAL_DEPTH_EDIT;
    coalesced_address_ = address;
    return true;
}

FLASHMEM bool MacroHistoryService::setModulationBindingEnabled(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId,
    bool enabled
) {
    using namespace core::state::modulation;
    auto* binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    if (binding == nullptr ||
        binding->destination != projectControlDestination(address)) {
        return false;
    }
    const bool current =
        (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
    if (current == enabled) return false;
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::SOURCE_STATE
    );
    if (!change) return false;
    if (!updateProjectModulationBinding(
            pages.control.authored.modulation,
            bindingId,
            binding->amountQ15,
            binding->application,
            binding->transfer,
            enabled,
            binding->slewMs
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

FLASHMEM bool MacroHistoryService::setAllModulationBindingsEnabled(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    bool enabled
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address)) return false;
    const auto destination = projectControlDestination(address);
    bool needsChange = false;
    const auto& graph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination == destination &&
            ((binding.flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U) !=
                enabled) {
            needsChange = true;
            break;
        }
    }
    if (!needsChange) return false;
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::SOURCE_STATE
    );
    if (!change) return false;
    auto& mutableGraph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < mutableGraph.outputBindingCount; ++index) {
        auto& binding = mutableGraph.outputBindings[index];
        if (binding.destination != destination) continue;
        binding.flags = enabled
            ? static_cast<uint8_t>(
                  binding.flags | PROJECT_MODULATION_BINDING_FLAG_ENABLED
              )
            : static_cast<uint8_t>(
                  binding.flags & ~PROJECT_MODULATION_BINDING_FLAG_ENABLED
              );
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

FLASHMEM bool MacroHistoryService::removeModulationBinding(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    core::state::modulation::ModulationBindingId bindingId
) {
    using namespace core::state::modulation;
    const auto* binding = findProjectModulationBinding(
        pages.control.authored.modulation,
        bindingId
    );
    if (binding == nullptr ||
        binding->destination != projectControlDestination(address)) {
        return false;
    }
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::REMOVE_MODULATOR_ASSIGNMENT
    );
    if (!change ||
        !removeProjectModulationBinding(
            pages.control.authored.modulation,
            bindingId
        ).changed()) {
        return false;
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

FLASHMEM bool MacroHistoryService::clearModulationBindings(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address)) return false;
    const auto destination = projectControlDestination(address);
    auto& graph = pages.control.authored.modulation;
    bool stored = false;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].destination == destination) {
            stored = true;
            break;
        }
    }
    if (!stored) return false;
    auto change = prepareModulationAssignments_(
        pages,
        address,
        MacroHistoryActionKind::CLEAR_MODULATION
    );
    if (!change) return false;
    for (uint16_t cursor = graph.outputBindingCount; cursor > 0U; --cursor) {
        const auto binding = graph.outputBindings[cursor - 1U];
        if (binding.destination == destination &&
            !removeProjectModulationBinding(graph, binding.id).changed()) {
            (void)applyModulationAssignments(
                pages,
                change->modulationAssignments->before
            );
            return false;
        }
    }
    pages.control.markAuthoredMutation();
    return commitModulationAssignments_(pages, std::move(change));
}

}  // namespace core::state::macro
