#include "handler/macro/MacroEditDomainServices.hpp"

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

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

}  // namespace

MacroEditDomainServices::MacroEditDomainServices(StateRefs state, Operations operations)
    : pages_(&state.pages)
    , macro_ui_(state.macroUi)
    , clipboard_(state.clipboard)
    , operations_(operations) {}

MacroEditDomainServices MacroEditDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroEditDomainServices{
        StateRefs{state.pages, &state.macroUi, &state.structureClipboard},
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

bool MacroEditDomainServices::automationManualOverrideActiveFor(uint8_t index) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return false;
    return (macro_ui_->automationManualOverrideMask.get() &
            static_cast<uint16_t>(1U << index)) != 0;
}

void MacroEditDomainServices::setAutomationManualOverride(uint8_t index, bool active) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return;
    const uint16_t bit = static_cast<uint16_t>(1U << index);
    const uint16_t current = macro_ui_->automationManualOverrideMask.get();
    const uint16_t next = active
        ? static_cast<uint16_t>(current | bit)
        : static_cast<uint16_t>(current & ~bit);
    if (next == current) return;
    macro_ui_->automationManualOverrideMask.set(next);
}

bool MacroEditDomainServices::clearAutomation(uint8_t index) const {
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        automationAddress(index)
    );
    if (slot == nullptr || !slot->automation.active) {
        return false;
    }

    core::state::macro::macroAutomationClearAutomation(pages_->automation, *slot);
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        setAutomationManualOverride(index, false);
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
    }
    return true;
}

bool MacroEditDomainServices::removeAutomation(uint8_t index) const {
    const bool removed = core::state::macro::macroAutomationClearSlot(
        pages_->automation,
        automationAddress(index)
    );
    if (!removed) {
        return false;
    }

    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        setAutomationManualOverride(index, false);
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
    }
    return true;
}

bool MacroEditDomainServices::copyAutomation(uint8_t index) const {
    if (clipboard_ == nullptr) return false;
    const auto* slot = automationSlot(index);
    if (slot == nullptr || !slot->automation.active) return false;
    clipboard_->storeMacroAutomation(pages_->automation, *slot);
    return true;
}

bool MacroEditDomainServices::pasteAutomation(uint8_t index) const {
    if (clipboard_ == nullptr || !clipboard_->hasMacroAutomation()) return false;
    if (!clipboard_->macroAutomationSet ||
        !clipboard_->macroAutomationSet->valid ||
        clipboard_->macroAutomationSet->count == 0) {
        return false;
    }
    const auto& entry = clipboard_->macroAutomationSet->entries[0];
    if (!entry.valid || !entry.state.automation.active) return false;

    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        pages_->automation,
        automationAddress(index)
    );
    if (slot == nullptr) return false;

    if (!core::state::macro::macroAutomationCopySlotState(
            pages_->automation,
            *slot,
            clipboard_->macroAutomationSet->pointPool,
            entry.state
        )) {
        return false;
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        setAutomationManualOverride(index, false);
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
    }
    return true;
}

}  // namespace core::handler
