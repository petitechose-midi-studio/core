#include "handler/macro/MacroEditDomainServices.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/macro/MacroAutomationClipboardOps.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {
namespace automation_clipboard_ops = core::handler::macro::automation_clipboard_ops;

namespace {

bool setConfigFromCoreState(void* context, uint8_t index, uint8_t channel, uint8_t cc) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr &&
           core::state::macro::MacroWorkflow::setConfig(*state, index, channel, cc);
}

void switchToPageFromCoreState(void* context, uint8_t pageIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToPage(*state, pageIndex);
}

void switchToTrackFromCoreState(void* context, uint8_t trackIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToTrack(*state, trackIndex);
}

void markProjectMutatedFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->markProjectMutated();
}

bool synchronizeSharedTrackStateFromCoreState(
    void* context,
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return false;
    if (state->currentSharedTrackEnabledMask() == enabledMask &&
        state->currentSharedActiveTrack() == activeTrack) {
        return true;
    }
    return state->setSharedTrackState(enabledMask, activeTrack);
}

bool computedSourcePlaybackActive(
    const core::state::macro::MacroAutomationSlotState* slot
) {
    return slot != nullptr &&
           (core::state::macro::macroCurvePlaybackActive(slot->automation) ||
            core::state::macro::macroCurvePlaybackActive(slot->modulation));
}

FLASHMEM bool auditionObjects(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::modulation::ModulatorSourceState*& source,
    core::state::modulation::ModulationBindingState*& binding
) {
    source = nullptr;
    binding = nullptr;
    const auto& audition = pages.control.audition;
    if (!audition.active ||
        audition.destination !=
            core::state::modulation::projectControlDestination(address)) {
        return false;
    }
    source = core::state::modulation::findProjectModulator(
        pages.control.authored.modulation,
        audition.sourceId
    );
    auto& graph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].id == audition.bindingId) {
            binding = &graph.outputBindings[index];
            break;
        }
    }
    return source != nullptr && binding != nullptr &&
           binding->sourceId == source->id &&
           binding->destination == audition.destination;
}

}  // namespace

MacroEditDomainServices::MacroEditDomainServices(StateRefs state, Operations operations)
    : pages_(&state.pages)
    , macro_ui_(state.macroUi)
    , clipboard_(state.clipboard)
    , macros_(state.macros)
    , history_(state.history)
    , operations_(operations) {}

MacroEditDomainServices MacroEditDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroEditDomainServices{
        StateRefs{
            state.pages,
            &state.macroUi,
            &state.structureClipboard,
            &state.macros,
            &state.macroHistory,
        },
        Operations{
            &state,
            setConfigFromCoreState,
            switchToPageFromCoreState,
            switchToTrackFromCoreState,
            markProjectMutatedFromCoreState,
            synchronizeSharedTrackStateFromCoreState,
        },
    };
}

bool MacroEditDomainServices::synchronizeSharedTrackState() const {
    return pages_ != nullptr &&
           operations_.synchronizeSharedTrackState != nullptr &&
           operations_.synchronizeSharedTrackState(
               operations_.context,
               pages_->currentTrackEnabledMask(),
               pages_->currentActiveTrack()
           );
}

void MacroEditDomainServices::publishModulationMutation_() const {
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
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

const core::state::macro::MacroConfig& MacroEditDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(*pages_, index);
}

bool MacroEditDomainServices::isMacroSlotActive(uint8_t index) const {
    return pages_ != nullptr && pages_->isMacroSlotActive(index);
}

bool MacroEditDomainServices::setConfig(uint8_t index, uint8_t channel, uint8_t cc) const {
    return operations_.setConfig != nullptr &&
           operations_.setConfig(operations_.context, index, channel, cc);
}

void MacroEditDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
    }
}

void MacroEditDomainServices::switchToTrack(uint8_t trackIndex) const {
    if (operations_.switchToTrack != nullptr) {
        operations_.switchToTrack(operations_.context, trackIndex);
    }
}

core::state::macro::MacroAutomationSlotAddress
MacroEditDomainServices::automationAddress(uint8_t index) const {
    return core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
}

const core::state::macro::MacroAutomationSlotState*
MacroEditDomainServices::automationSlot(uint8_t index) const {
    if (pages_ == nullptr ||
        !core::state::modulation::readProjectControlMacroSlot(
            pages_->control,
            automationAddress(index),
            slot_view_cache_
        ) || !slot_view_cache_.present) {
        return nullptr;
    }
    return &slot_view_cache_.compatibility;
}

bool MacroEditDomainServices::automationClipboardAvailable() const {
    return clipboard_ != nullptr && clipboard_->hasMacroAutomation();
}

bool MacroEditDomainServices::automationActiveFor(uint8_t index) const {
    const auto* slot = automationSlot(index);
    return slot != nullptr && slot->automation.active;
}

bool MacroEditDomainServices::automationStoredFor(uint8_t index) const {
    const auto* slot = automationSlot(index);
    return slot != nullptr &&
           core::state::macro::macroCurveStored(slot->automation);
}

bool MacroEditDomainServices::automationPlaybackActiveFor(uint8_t index) const {
    const auto* slot = automationSlot(index);
    return slot != nullptr &&
           core::state::macro::macroCurvePlaybackActive(slot->automation);
}

bool MacroEditDomainServices::modulationStoredFor(uint8_t index) const {
    core::state::modulation::ProjectControlMacroSlotView slot{};
    return pages_ != nullptr &&
           core::state::modulation::readProjectControlMacroSlot(
               pages_->control,
               automationAddress(index),
               slot
           ) && slot.modulationStored;
}

bool MacroEditDomainServices::modulationPlaybackActiveFor(uint8_t index) const {
    core::state::modulation::ProjectControlMacroSlotView slot{};
    return pages_ != nullptr &&
           core::state::modulation::readProjectControlMacroSlot(
               pages_->control,
               automationAddress(index),
               slot
           ) && slot.activeModulationCount > 0U;
}

float MacroEditDomainServices::modulationDepth(uint8_t index) const {
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

core::state::macro::MacroModulationOrigin
MacroEditDomainServices::modulationOrigin(uint8_t index) const {
    const auto* slot = automationSlot(index);
    return slot != nullptr ? slot->modulation.modulationOrigin
                           : core::state::macro::MacroModulationOrigin::NATIVE;
}

MacroSourceMode MacroEditDomainServices::sourceModeFor(uint8_t index) const {
    const auto* slot = automationSlot(index);
    if (manualOverrideActiveFor(index)) return MacroSourceMode::MANUAL;
    if (slot == nullptr) return MacroSourceMode::OFF;
    if (core::state::macro::macroCurveSuspendedAfterRecord(slot->modulation)) {
        return MacroSourceMode::SUSPENDED;
    }
    const bool automation =
        core::state::macro::macroCurvePlaybackActive(slot->automation);
    const bool modulation =
        core::state::macro::macroCurvePlaybackActive(slot->modulation);
    if (automation && modulation) return MacroSourceMode::AUTO_MOD;
    if (automation) return MacroSourceMode::AUTOMATION;
    if (modulation && slot->modulationDepth <= 0.0f) {
        return MacroSourceMode::PAUSED;
    }
    if (modulation) return MacroSourceMode::MODULATION;
    return MacroSourceMode::OFF;
}

bool MacroEditDomainServices::manualOverrideActiveFor(uint8_t index) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return false;
    return macro_ui_->manualOverrides.activeFor(automationAddress(index));
}

void MacroEditDomainServices::setManualOverride(uint8_t index, bool active) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return;
    const auto address = automationAddress(index);
    if (active) {
        const auto* slot = automationSlot(index);
        if (!computedSourcePlaybackActive(slot)) return;
        const float value = macros_ != nullptr
            ? core::state::macro::MacroWorkflow::runtimeValue(*macros_, index)
            : pages_->activePageData().values[index];
        (void)macro_ui_->manualOverrides.activate(address, value);
    } else {
        (void)resumeSources(index);
        return;
    }
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
}

bool MacroEditDomainServices::setAutomationPlayback(
    uint8_t index,
    bool active
) const {
    const auto address = automationAddress(index);
    core::state::modulation::ProjectControlMacroSlotView slot{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages_->control,
            address,
            slot
        ) || !slot.automationStored || slot.automationEnabled == active) {
        return false;
    }

    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::SOURCE_STATE
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!core::state::modulation::setProjectControlAutomationEnabled(
            pages_->control,
            address,
            active
        )) {
        return false;
    }
    if (history_ != nullptr &&
        !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroEditDomainServices::setModulationPlayback(
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

const core::state::modulation::ModulationBindingState*
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

bool MacroEditDomainServices::setFocusedModulationPlayback(
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

bool MacroEditDomainServices::removeFocusedModulation(uint8_t index) const {
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

FLASHMEM bool MacroEditDomainServices::clearAutomation(uint8_t index) const {
    const auto address = automationAddress(index);
    core::state::modulation::ProjectControlMacroSlotView before{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages_->control,
            address,
            before
        ) || !before.automationStored) {
        return false;
    }

    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::CLEAR_AUTOMATION
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!core::state::modulation::clearProjectControlAutomation(
            pages_->control,
            address
        )) {
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        core::state::modulation::ProjectControlMacroSlotView after{};
        const bool hasComputedSource =
            core::state::modulation::readProjectControlMacroSlot(
                pages_->control,
                address,
                after
            ) && after.present &&
            (after.automationEnabled || after.modulationEnabled);
        if (!hasComputedSource) {
            (void)macro_ui_->manualOverrides.resume(address);
            macro_ui_->refreshManualOverrideMask(
                pages_->currentActiveTrack(),
                pages_->currentActivePage()
            );
        }
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
    }
    return true;
}

bool MacroEditDomainServices::removeAutomation(uint8_t index) const {
    const auto address = automationAddress(index);
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::REMOVE_SLOT
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    const core::state::macro::MacroAutomationSlotState empty{};
    const bool removed = core::state::modulation::replaceProjectControlMacroSlot(
        pages_->control,
        address,
        empty,
        nullptr,
        0
    );
    if (!removed) {
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        (void)macro_ui_->manualOverrides.resume(address);
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::copyAutomation(uint8_t index) const {
    if (clipboard_ == nullptr) return false;
    return automation_clipboard_ops::copySlotAutomationToClipboard(
        pages_->control,
        automationAddress(index),
        *clipboard_
    );
}

FLASHMEM bool MacroEditDomainServices::copyDestination(uint8_t index) const {
    return clipboard_ != nullptr &&
           automation_clipboard_ops::copyDestinationToClipboard(
               *pages_,
               automationAddress(index),
               *clipboard_
           );
}

automation_clipboard_ops::MacroTypedPastePreflight
MacroEditDomainServices::preflightDestinationPaste(uint8_t index) const {
    if (clipboard_ == nullptr) return {};
    return automation_clipboard_ops::preflightDestinationPaste(
        *pages_,
        automationAddress(index),
        *clipboard_
    );
}

FLASHMEM bool MacroEditDomainServices::pasteDestination(
    uint8_t index,
    bool overwriteConfirmed
) const {
    if (clipboard_ == nullptr) return false;
    const auto plan = preflightDestinationPaste(index);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    const auto address = automationAddress(index);
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::PASTE_DESTINATION
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    const uint8_t cc = clipboard_->macroAutomationSet->sourceCc;
    const uint8_t channel = activeConfig(index).channel;
    if (!setConfig(index, channel, cc)) return false;
    if (history_ != nullptr &&
        !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    return true;
}

automation_clipboard_ops::MacroTypedPastePreflight
MacroEditDomainServices::preflightAutomationPaste(uint8_t index) const {
    if (clipboard_ == nullptr) return {};
    return automation_clipboard_ops::preflightAutomationPaste(
        *pages_,
        automationAddress(index),
        *clipboard_
    );
}

FLASHMEM bool MacroEditDomainServices::pasteAutomation(uint8_t index) const {
    return pasteAutomation(index, true);
}

FLASHMEM bool MacroEditDomainServices::pasteAutomation(
    uint8_t index,
    bool overwriteConfirmed
) const {
    if (clipboard_ == nullptr) return false;
    const auto address = automationAddress(index);
    const auto plan = preflightAutomationPaste(index);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::PASTE_AUTOMATION
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!automation_clipboard_ops::pasteAutomationFromClipboard(
            *pages_,
            address,
            *clipboard_,
            overwriteConfirmed
        )) {
        return false;
    }

    // Reapplying the same Automation is still meaningful when it exits Manual,
    // but it must not invent a Project mutation or an empty Undo entry.
    const bool projectChanged = change == nullptr ||
        !core::state::macro::liveMacroSlotMatchesHistorySnapshot(
            *pages_,
            change->slot->before
        );
    if (history_ != nullptr && projectChanged &&
        !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }

    bool manualChanged = false;
    if (macro_ui_ != nullptr) {
        manualChanged = macro_ui_->manualOverrides.resume(address);
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        if (projectChanged || manualChanged) {
            macro_ui_->automationRecordingRevision.set(
                macro_ui_->automationRecordingRevision.get() + 1U
            );
        }
    }
    if (projectChanged && operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return projectChanged || manualChanged;
}

core::state::macro::MacroAutomationConversionPlan
MacroEditDomainServices::preflightConversion(
    uint8_t index,
    core::state::macro::MacroAutomationConversionPolicy policy
) const {
    if (pages_ == nullptr || index >= core::state::macro::MACRO_COUNT) return {};
    return core::state::modulation::preflightProjectControlConversion(
        pages_->control,
        automationAddress(index),
        policy,
        pages_->activePageData().values[index]
    );
}

FLASHMEM bool MacroEditDomainServices::applyConversion(
    uint8_t index,
    const core::state::macro::MacroAutomationConversionPlan& plan,
    bool overwriteConfirmed
) const {
    if (index >= core::state::macro::MACRO_COUNT ||
        !core::state::macro::macroAutomationAddressEquals(
            plan.address,
            automationAddress(index)
        )) {
        return false;
    }
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              plan.address,
              core::state::macro::MacroHistoryActionKind::CONVERT_AUTOMATION
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;

    auto& base = pages_->activePageData().values[index];
    if (!core::state::modulation::applyProjectControlConversion(
            pages_->control,
            base,
            plan,
            overwriteConfirmed
        )) {
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        (void)macro_ui_->manualOverrides.resume(plan.address);
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    if (macros_ != nullptr) {
        core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, base);
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroEditDomainServices::resumeSources(uint8_t index) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return false;
    const auto address = automationAddress(index);
    const bool hadManual = macro_ui_->manualOverrides.activeFor(address);
    if (!hadManual) return false;
    (void)macro_ui_->manualOverrides.resume(address);
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
    macro_ui_->automationRecordingRevision.set(
        macro_ui_->automationRecordingRevision.get() + 1U
    );
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

FLASHMEM bool MacroEditDomainServices::removeSlot(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT ||
        !pages_->isMacroSlotActive(index) || history_ == nullptr) {
        return false;
    }
    const auto address = automationAddress(index);
    if (!history_->removeMacroSlot(*pages_, address)) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    if (macros_ != nullptr) {
        core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, 0.5f);
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroEditDomainServices::copySlot(uint8_t index) const {
    return clipboard_ != nullptr &&
           automation_clipboard_ops::copySlotToClipboard(
               *pages_,
               automationAddress(index),
               *clipboard_
           );
}

automation_clipboard_ops::MacroTypedPastePreflight
MacroEditDomainServices::preflightSlotPaste(uint8_t index) const {
    if (clipboard_ == nullptr) return {};
    return automation_clipboard_ops::preflightSlotPaste(
        *pages_,
        automationAddress(index),
        *clipboard_
    );
}

bool MacroEditDomainServices::pasteSlot(
    uint8_t index,
    bool overwriteConfirmed
) const {
    if (clipboard_ == nullptr) return false;
    const auto address = automationAddress(index);
    const auto plan = preflightSlotPaste(index);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::PASTE_SLOT
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!automation_clipboard_ops::pasteSlotFromClipboard(
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
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    if (macros_ != nullptr) {
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
            *macros_,
            *pages_
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
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

automation_clipboard_ops::MacroTypedPastePreflight
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
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
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
        .channel = PROJECT_MODULATION_TRIGGER_ANY_CHANNEL,
        .data = PROJECT_MODULATION_TRIGGER_ANY_NOTE,
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

FLASHMEM bool MacroEditDomainServices::setLfoAuditionShape(
    uint8_t index,
    core::state::modulation::ModulatorLfoShape shape
) const {
    using namespace core::state::modulation;
    if (static_cast<uint8_t>(shape) >
        static_cast<uint8_t>(ModulatorLfoShape::SQUARE)) {
        return false;
    }
    ModulatorSourceState* source = nullptr;
    ModulationBindingState* binding = nullptr;
    if (pages_ == nullptr || !auditionObjects(
            *pages_, automationAddress(index), source, binding
        ) || source->kind != ModulatorKind::LFO ||
        source->parameters.lfo.shape == shape) {
        return false;
    }
    source->parameters.lfo.shape = shape;
    pages_->control.markAuthoredMutation();
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::setLfoAuditionPeriodTicks(
    uint8_t index,
    uint32_t periodTicks
) const {
    using namespace core::state::modulation;
    ModulatorSourceState* source = nullptr;
    ModulationBindingState* binding = nullptr;
    if (periodTicks == 0U || pages_ == nullptr || !auditionObjects(
            *pages_, automationAddress(index), source, binding
        ) || source->kind != ModulatorKind::LFO ||
        source->parameters.lfo.periodTicks == periodTicks) {
        return false;
    }
    source->parameters.lfo.periodTicks = periodTicks;
    pages_->control.markAuthoredMutation();
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::setAdsrAuditionParameters(
    uint8_t index,
    const core::state::modulation::ModulatorAdsrParameters& parameters
) const {
    using namespace core::state::modulation;
    if (!validProjectModulatorAdsrParameters(parameters)) return false;
    ModulatorSourceState* source = nullptr;
    ModulationBindingState* binding = nullptr;
    if (pages_ == nullptr || !auditionObjects(
            *pages_, automationAddress(index), source, binding
        ) || source->kind != ModulatorKind::ADSR ||
        std::memcmp(
            &source->parameters.adsr,
            &parameters,
            sizeof(parameters)
        ) == 0) {
        return false;
    }
    source->parameters.adsr = parameters;
    pages_->control.markAuthoredMutation();
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::setModulatorAuditionDepthQ15(
    uint8_t index,
    int16_t depthQ15
) const {
    using namespace core::state::modulation;
    if (depthQ15 == std::numeric_limits<int16_t>::min()) return false;
    ModulatorSourceState* source = nullptr;
    ModulationBindingState* binding = nullptr;
    if (pages_ == nullptr || !auditionObjects(
            *pages_, automationAddress(index), source, binding
        ) || binding->amountQ15 == depthQ15) {
        return false;
    }
    binding->amountQ15 = depthQ15;
    pages_->control.markAuthoredMutation();
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::cancelModulatorAudition(
    uint8_t index
) const {
    if (pages_ == nullptr || history_ == nullptr ||
        !history_->cancelModulatorAudition(*pages_, automationAddress(index))) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::applyModulatorAudition(
    uint8_t index
) const {
    const auto bindingId = pages_ != nullptr
        ? pages_->control.audition.bindingId
        : core::state::modulation::ModulationBindingId{};
    if (pages_ == nullptr || history_ == nullptr ||
        !history_->commitModulatorAudition(*pages_, automationAddress(index))) {
        return false;
    }
    (void)focusModulationBinding(index, bindingId);
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
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

bool MacroEditDomainServices::setModulationDepth(uint8_t index, float depth) const {
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

void MacroEditDomainServices::endDepthGesture() const {
    if (history_ != nullptr) history_->endCoalescing();
}

bool MacroEditDomainServices::undo() const {
    if (history_ == nullptr) return false;
    core::state::macro::MacroAutomationSlotAddress address{};
    if (!history_->undo(*pages_, &address)) return false;
    (void)synchronizeSharedTrackState();
    if (macro_ui_ != nullptr) {
        (void)macro_ui_->manualOverrides.resume(address);
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    if (macros_ != nullptr) {
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
            *macros_,
            *pages_
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroEditDomainServices::redo() const {
    if (history_ == nullptr) return false;
    core::state::macro::MacroAutomationSlotAddress address{};
    if (!history_->redo(*pages_, &address)) return false;
    (void)synchronizeSharedTrackState();
    if (macro_ui_ != nullptr) {
        (void)macro_ui_->manualOverrides.resume(address);
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationRecordingRevision.set(
            macro_ui_->automationRecordingRevision.get() + 1U
        );
    }
    if (macros_ != nullptr) {
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
            *macros_,
            *pages_
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroEditDomainServices::setAutomationDurationBeats(uint8_t index,
                                                         float durationBeats) const {
    const bool changed =
        core::state::modulation::setProjectControlAutomationDurationBeats(
        pages_->control,
        automationAddress(index),
        durationBeats
    );
    if (!changed) return false;

    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
    }
    return true;
}

bool MacroEditDomainServices::setAutomationWindowOffsetBeats(uint8_t index,
                                                             float offsetBeats) const {
    const bool changed =
        core::state::modulation::setProjectControlAutomationWindowOffsetBeats(
        pages_->control,
        automationAddress(index),
        offsetBeats
    );
    if (!changed) return false;

    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
    }
    return true;
}

}  // namespace core::handler
