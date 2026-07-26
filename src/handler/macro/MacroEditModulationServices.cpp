#include "handler/macro/MacroEditDomainServices.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/macro/MacroAutomationClipboardOps.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {
namespace automation_clipboard_ops = core::handler::macro::automation_clipboard_ops;
FLASHMEM void MacroEditDomainServices::publishModulationMutation_() const {
    if (macro_ui_ != nullptr) {
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
        );
        macro_ui_->runtimeProjectionRevision.set(
            core::state::macro::nextMacroRuntimeProjectionRevision(
                macro_ui_->runtimeProjectionRevision.get(),
                core::state::macro::kMacroRuntimeProjectionDirtyConfig
            )
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
}

FLASHMEM bool MacroEditDomainServices::modulationStoredFor(uint8_t index) const {
    core::state::modulation::ProjectControlMacroDestinationView view{};
    return pages_ != nullptr &&
           core::state::modulation::readProjectControlMacroDestination(
               pages_->control,
               automationAddress(index),
               view
           ) && view.modulationCount > 0U;
}

FLASHMEM bool MacroEditDomainServices::modulationPlaybackActiveFor(
    uint8_t index
) const {
    core::state::modulation::ProjectControlMacroDestinationView view{};
    return pages_ != nullptr &&
           core::state::modulation::readProjectControlMacroDestination(
               pages_->control,
               automationAddress(index),
               view
           ) && view.activeModulationCount > 0U;
}

FLASHMEM float MacroEditDomainServices::modulationDepth(uint8_t index) const {
    const auto* binding = focusedModulationBindingState(index);
    return binding != nullptr
        ? std::clamp(
              static_cast<float>(binding->amountQ15) / 32767.0f,
              -1.0f,
              1.0f
          )
        : 0.0f;
}

FLASHMEM uint16_t MacroEditDomainServices::modulationGlobalDepthQ15(
    uint8_t index
) const {
    if (pages_ == nullptr || index >= core::state::macro::MACRO_COUNT) {
        return core::state::modulation::
            PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    }
    return core::state::modulation::projectModulationDestinationScaleQ15(
        pages_->control.authored.modulation,
        core::state::modulation::projectControlDestination(
            automationAddress(index)
        )
    );
}

FLASHMEM bool MacroEditDomainServices::setModulationPlayback(
    uint8_t index,
    bool active
) const {
    if (history_ == nullptr ||
        !history_->setAllModulationBindingsEnabled(
            *pages_,
            automationAddress(index),
            active
        )) {
        return false;
    }
    publishModulationMutation_();
    return true;
}

FLASHMEM const core::state::modulation::ModulationBindingState*
MacroEditDomainServices::focusedModulationBindingState(uint8_t index) const {
    if (pages_ == nullptr || index >= core::state::macro::MACRO_COUNT) {
        return nullptr;
    }
    const auto bindingId = focusedModulationBinding(index);
    return core::state::modulation::findProjectModulationBinding(
        pages_->control.authored.modulation,
        bindingId
    );
}

FLASHMEM bool MacroEditDomainServices::setFocusedModulationPlayback(
    uint8_t index,
    bool active
) const {
    if (history_ == nullptr) return false;
    const auto bindingId = focusedModulationBinding(index);
    if (!history_->setModulationBindingEnabled(
            *pages_,
            automationAddress(index),
            bindingId,
            active
        )) {
        return false;
    }
    publishModulationMutation_();
    return true;
}

FLASHMEM bool MacroEditDomainServices::removeFocusedModulation(uint8_t index) const {
    if (history_ == nullptr) return false;
    const auto bindingId = focusedModulationBinding(index);
    if (!history_->removeModulationBinding(
            *pages_,
            automationAddress(index),
            bindingId
        )) {
        return false;
    }
    publishModulationMutation_();
    return true;
}

FLASHMEM bool MacroEditDomainServices::clearModulation(uint8_t index) const {
    if (history_ == nullptr ||
        !history_->clearModulationBindings(
            *pages_,
            automationAddress(index)
        )) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
    }
    publishModulationMutation_();
    return true;
}

FLASHMEM bool MacroEditDomainServices::copyModulation(uint8_t index) const {
    if (clipboard_ == nullptr || pages_ == nullptr) return false;
    const auto address = automationAddress(index);
    const auto* binding = focusedModulationBindingState(index);
    if (binding != nullptr) {
        return automation_clipboard_ops::copyModulationAssignmentToClipboard(
            pages_->control,
            address,
            binding->id,
            *clipboard_
        );
    }
    if (!modulationStoredFor(index)) return false;
    return automation_clipboard_ops::copyModulationToClipboard(
        pages_->control,
        address,
        *clipboard_
    );
}

FLASHMEM bool MacroEditDomainServices::hasModulationAssignmentClipboard() const {
    return clipboard_ != nullptr &&
           clipboard_->hasMacroModulationAssignment();
}

FLASHMEM automation_clipboard_ops::MacroTypedPastePreflight
MacroEditDomainServices::preflightModulationPaste(uint8_t index) const {
    if (clipboard_ == nullptr) return {};
    return automation_clipboard_ops::preflightModulationPaste(
        *pages_,
        automationAddress(index),
        *clipboard_
    );
}

FLASHMEM bool MacroEditDomainServices::pasteModulation(
    uint8_t index,
    bool overwriteConfirmed
) const {
    if (clipboard_ == nullptr) return false;
    const auto address = automationAddress(index);
    const auto plan = preflightModulationPaste(index);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    if (clipboard_->hasMacroModulationAssignment()) {
        if (history_ == nullptr) return false;
        core::state::modulation::ModulationBindingDraft draft{};
        if (!automation_clipboard_ops::modulationAssignmentDraftFromClipboard(
                *clipboard_,
                core::state::modulation::projectControlDestination(address),
                draft
            )) {
            return false;
        }
        core::state::modulation::ModulationBindingId appliedBinding{};
        if (!history_->pasteModulationBinding(
                *pages_,
                address,
                draft,
                overwriteConfirmed,
                &appliedBinding
            )) {
            return false;
        }
        (void)focusModulationBinding(index, appliedBinding);
        publishModulationMutation_();
        return true;
    }
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::PASTE_MODULATION
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!automation_clipboard_ops::pasteModulationFromClipboard(
            *pages_,
            address,
            *clipboard_,
            overwriteConfirmed
        )) {
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroEditDomainServices::beginDefaultLfoAudition(uint8_t index) const {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (pages_ == nullptr || history_ == nullptr ||
        index >= core::state::macro::MACRO_COUNT) {
        return failure;
    }
    const auto address = automationAddress(index);
    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    formatNextProjectLfoName(
        pages_->control.authored.modulation,
        name,
        sizeof(name)
    );
    ModulatorLfoDraft source{};
    source.name = name;
    source.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    source.parameters.shape = ModulatorLfoShape::SINE;
    source.parameters.retrigger = ModulatorRetriggerPolicy::TRANSPORT;
    source.parameters.timing = ModulatorTimingMode::SYNC;

    ModulationBindingDraft binding{};
    binding.destination = projectControlDestination(address);
    binding.amountQ15 = 8192;
    binding.application = ModulationApplication::NATURAL;
    return history_->beginLfoModulatorAudition(
        *pages_,
        address,
        source,
        binding
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroEditDomainServices::beginDefaultAdsrAudition(uint8_t index) const {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (pages_ == nullptr || history_ == nullptr ||
        index >= core::state::macro::MACRO_COUNT) {
        return failure;
    }
    const auto address = automationAddress(index);
    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    formatNextProjectModulatorName(
        pages_->control.authored.modulation,
        ModulatorKind::ADSR,
        name,
        sizeof(name)
    );
    ModulatorAdsrDraft source{};
    source.name = name;

    ModulationTriggerDraft trigger{};
    trigger.trigger = {
        .kind = ModulationTriggerKind::TRACK_NOTE,
        .track = address.track,
        .noteMin = 0U,
        .noteMax = 127U,
    };

    ModulationBindingDraft binding{};
    binding.destination = projectControlDestination(address);
    binding.amountQ15 = 8192;
    binding.application = ModulationApplication::NATURAL;
    return history_->beginAdsrModulatorAudition(
        *pages_, address, source, trigger, binding
    );
}

FLASHMEM core::state::modulation::ProjectModulationResult
MacroEditDomainServices::beginExistingModulatorAudition(
    uint8_t index,
    core::state::modulation::ModulatorId sourceId
) const {
    using namespace core::state::modulation;
    ProjectModulationResult failure{};
    failure.status = ProjectModulationStatus::INVALID_ARGUMENT;
    if (pages_ == nullptr || history_ == nullptr ||
        index >= core::state::macro::MACRO_COUNT || !valid(sourceId)) {
        return failure;
    }
    const auto address = automationAddress(index);
    ModulationBindingDraft binding{};
    binding.sourceId = sourceId;
    binding.destination = projectControlDestination(address);
    binding.amountQ15 = 8192;
    binding.application = ModulationApplication::NATURAL;
    return history_->beginExistingModulatorAudition(
        *pages_,
        address,
        sourceId,
        binding
    );
}

FLASHMEM bool MacroEditDomainServices::cancelModulatorAudition(
    uint8_t index
) const {
    if (pages_ == nullptr || history_ == nullptr ||
        !history_->cancelModulatorAudition(*pages_, automationAddress(index))) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM core::state::modulation::ModulationBindingId
MacroEditDomainServices::focusedModulationBinding(uint8_t index) const {
    if (pages_ == nullptr || index >= core::state::macro::MACRO_COUNT) return {};
    return core::state::modulation::projectControlFocusedModulationBinding(
        pages_->control,
        automationAddress(index)
    );
}

FLASHMEM bool MacroEditDomainServices::focusModulationBinding(
    uint8_t index,
    core::state::modulation::ModulationBindingId bindingId
) const {
    return pages_ != nullptr && index < core::state::macro::MACRO_COUNT &&
           core::state::modulation::setProjectControlFocusedModulationBinding(
               pages_->control,
               automationAddress(index),
               bindingId
           );
}

FLASHMEM bool MacroEditDomainServices::setModulationDepth(uint8_t index,
                                                         float depth) const {
    if (history_ == nullptr || index >= core::state::macro::MACRO_COUNT) return false;
    if (!history_->setModulationDepthCoalesced(
            *pages_,
            automationAddress(index),
            depth
        )) {
        return false;
    }
    publishModulationMutation_();
    return true;
}

FLASHMEM bool MacroEditDomainServices::setModulationGlobalDepthQ15(
    uint8_t index,
    uint16_t scaleQ15
) const {
    if (history_ == nullptr || index >= core::state::macro::MACRO_COUNT) {
        return false;
    }
    if (!history_->setModulationDestinationScaleCoalesced(
            *pages_,
            automationAddress(index),
            scaleQ15
        )) {
        return false;
    }
    publishModulationMutation_();
    return true;
}

FLASHMEM void MacroEditDomainServices::endDepthGesture() const {
    if (history_ != nullptr) history_->endCoalescing();
}
}  // namespace core::handler
