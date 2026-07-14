#include "handler/macro/MacroEditDomainServices.hpp"

#include <utility>

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

void markProjectMutatedFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->markProjectMutated();
}

bool computedSourcePlaybackActive(
    const core::state::macro::MacroAutomationSlotState* slot
) {
    return slot != nullptr &&
           (core::state::macro::macroCurvePlaybackActive(slot->automation) ||
            core::state::macro::macroCurvePlaybackActive(slot->modulation));
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
            markProjectMutatedFromCoreState,
        },
    };
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
    return core::state::macro::macroAutomationFindSlot(
        pages_->automation,
        automationAddress(index)
    );
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

bool MacroEditDomainServices::modulationStoredFor(uint8_t index) const {
    const auto* slot = automationSlot(index);
    return slot != nullptr &&
           core::state::macro::macroCurveStored(slot->modulation);
}

float MacroEditDomainServices::modulationDepth(uint8_t index) const {
    const auto* slot = automationSlot(index);
    return slot != nullptr ? core::state::macro::macroAutomationClamp01(
                                 slot->modulationDepth
                             )
                           : 0.0f;
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
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        address
    );
    if (slot == nullptr ||
        !core::state::macro::macroCurveStored(slot->automation)) {
        return false;
    }
    const auto next = active
        ? core::state::macro::MacroCurvePlaybackState::ACTIVE
        : core::state::macro::MacroCurvePlaybackState::OFF;
    if (slot->automation.playbackState == next) return false;

    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::SOURCE_STATE
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    slot->automation.playbackState = next;
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
    const auto address = automationAddress(index);
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        address
    );
    if (slot == nullptr ||
        !core::state::macro::macroCurveStored(slot->modulation)) {
        return false;
    }
    const auto next = active
        ? core::state::macro::MacroCurvePlaybackState::ACTIVE
        : core::state::macro::MacroCurvePlaybackState::OFF;
    if (slot->modulation.playbackState == next) return false;

    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::SOURCE_STATE
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    slot->modulation.playbackState = next;
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

bool MacroEditDomainServices::clearAutomation(uint8_t index) const {
    const auto address = automationAddress(index);
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        address
    );
    if (slot == nullptr || !slot->automation.active) {
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
    core::state::macro::macroAutomationClearAutomation(pages_->automation, *slot);
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        if (!computedSourcePlaybackActive(slot)) {
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
    const bool removed = core::state::macro::macroAutomationClearSlot(
        pages_->automation,
        address
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

bool MacroEditDomainServices::copyAutomation(uint8_t index) const {
    if (clipboard_ == nullptr) return false;
    return automation_clipboard_ops::copySlotAutomationToClipboard(
        pages_->automation,
        automationAddress(index),
        *clipboard_
    );
}

bool MacroEditDomainServices::pasteAutomation(uint8_t index) const {
    if (clipboard_ == nullptr) return false;
    const auto address = automationAddress(index);
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::PASTE_SLOT
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!automation_clipboard_ops::pasteFirstClipboardAutomationToSlot(
        pages_->automation,
        address,
        *clipboard_
    )) {
        if (change) {
            (void)core::state::macro::applyMacroSlotHistorySnapshot(
                *pages_,
                change->before
            );
        }
        return false;
    }

    // Reapplying the same Automation is still meaningful when it exits Manual,
    // but it must not invent a Project mutation or an empty Undo entry.
    const bool projectChanged = change == nullptr ||
        !core::state::macro::liveMacroSlotMatchesHistorySnapshot(
            *pages_,
            change->before
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
    return core::state::macro::macroAutomationPreflightConversion(
        pages_->automation,
        automationAddress(index),
        policy,
        pages_->activePageData().values[index]
    );
}

bool MacroEditDomainServices::applyConversion(
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
    if (!core::state::macro::macroAutomationApplyConversion(
            pages_->automation,
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

bool MacroEditDomainServices::enableAutoMod(uint8_t index) const {
    const auto address = automationAddress(index);
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        address
    );
    if (slot == nullptr ||
        !core::state::macro::macroCurveStored(slot->automation) ||
        !core::state::macro::macroCurveStored(slot->modulation)) {
        return false;
    }
    if (core::state::macro::macroCurvePlaybackActive(slot->automation) &&
        core::state::macro::macroCurvePlaybackActive(slot->modulation) &&
        !manualOverrideActiveFor(index)) {
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
    slot->automation.playbackState =
        core::state::macro::MacroCurvePlaybackState::ACTIVE;
    slot->modulation.playbackState =
        core::state::macro::MacroCurvePlaybackState::ACTIVE;
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
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
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroEditDomainServices::resumeSources(uint8_t index) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return false;
    const auto address = automationAddress(index);
    const bool hadManual = macro_ui_->manualOverrides.activeFor(address);
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        address
    );
    const bool suspended = slot != nullptr &&
        core::state::macro::macroCurveSuspendedAfterRecord(slot->modulation);
    if (!hadManual && !suspended) return false;

    auto change = suspended && history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::SOURCE_STATE
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (suspended && history_ != nullptr && !change) return false;
    if (suspended) {
        slot->modulation.playbackState =
            core::state::macro::MacroCurvePlaybackState::ACTIVE;
        if (history_ != nullptr && !history_->commitPrepared(
                *pages_,
                std::move(change)
            )) {
            return false;
        }
    }
    (void)macro_ui_->manualOverrides.resume(address);
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
    macro_ui_->automationRecordingRevision.set(
        macro_ui_->automationRecordingRevision.get() + 1U
    );
    if (suspended && operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroEditDomainServices::clearModulation(uint8_t index) const {
    const auto address = automationAddress(index);
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        address
    );
    if (slot == nullptr ||
        !core::state::macro::macroCurveStored(slot->modulation)) {
        return false;
    }
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::CLEAR_MODULATION
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    core::state::macro::macroAutomationClearModulation(pages_->automation, *slot);
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        if (!computedSourcePlaybackActive(slot)) {
            (void)macro_ui_->manualOverrides.resume(address);
        }
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

bool MacroEditDomainServices::removeSlot(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT ||
        !pages_->isMacroSlotActive(index)) {
        return false;
    }
    const auto address = automationAddress(index);
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::REMOVE_SLOT
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;

    (void)core::state::macro::macroAutomationClearSlot(
        pages_->automation,
        address
    );
    auto& page = pages_->activePageData();
    page.setMacroActive(index, false);
    page.cc[index] = 0;
    page.values[index] = 0.5f;
    pages_->updateActiveConfigs();
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
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
        if (change) {
            (void)core::state::macro::applyMacroSlotHistorySnapshot(
                *pages_,
                change->before
            );
        }
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
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

bool MacroEditDomainServices::copyModulation(uint8_t index) const {
    return clipboard_ != nullptr &&
           automation_clipboard_ops::copyModulationToClipboard(
               pages_->automation,
               automationAddress(index),
               *clipboard_
           );
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

bool MacroEditDomainServices::pasteModulation(
    uint8_t index,
    bool overwriteConfirmed
) const {
    if (clipboard_ == nullptr) return false;
    const auto address = automationAddress(index);
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
        if (change) {
            (void)core::state::macro::applyMacroSlotHistorySnapshot(
                *pages_,
                change->before
            );
        }
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
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
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
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

void MacroEditDomainServices::endDepthGesture() const {
    if (history_ != nullptr) history_->endCoalescing();
}

bool MacroEditDomainServices::undo() const {
    if (history_ == nullptr) return false;
    core::state::macro::MacroAutomationSlotAddress address{};
    if (!history_->undo(*pages_, &address)) return false;
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
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        automationAddress(index)
    );
    if (slot == nullptr || !slot->automation.active) {
        return false;
    }

    const bool changed = core::state::macro::macroAutomationResizeCurveDuration(
        slot->automation,
        pages_->automation.pointPool,
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
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        automationAddress(index)
    );
    if (slot == nullptr || !slot->automation.active) {
        return false;
    }

    const bool changed = core::state::macro::macroAutomationSetCurveWindowOffset(
        slot->automation,
        pages_->automation.pointPool,
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
