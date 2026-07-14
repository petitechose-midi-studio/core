#include "handler/macro/MacroEditDomainServices.hpp"

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
    , operations_(operations) {}

MacroEditDomainServices MacroEditDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroEditDomainServices{
        StateRefs{state.pages, &state.macroUi, &state.structureClipboard, &state.macros},
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
        (void)macro_ui_->manualOverrides.resume(address);
        auto* slot = core::state::macro::macroAutomationFindMutableSlot(
            pages_->automation,
            address
        );
        if (slot != nullptr &&
            core::state::macro::macroCurveSuspendedAfterRecord(slot->modulation)) {
            slot->modulation.playbackState =
                core::state::macro::MacroCurvePlaybackState::ACTIVE;
            macro_ui_->automationRecordingRevision.set(
                macro_ui_->automationRecordingRevision.get() + 1U
            );
            if (operations_.markProjectMutated != nullptr) {
                operations_.markProjectMutated(operations_.context);
            }
        }
    }
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
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
        if (!computedSourcePlaybackActive(slot)) {
            (void)macro_ui_->manualOverrides.resume(automationAddress(index));
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
        (void)macro_ui_->manualOverrides.resume(automationAddress(index));
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
    if (!automation_clipboard_ops::pasteFirstClipboardAutomationToSlot(
        pages_->automation,
        automationAddress(index),
        *clipboard_
    )) {
        return false;
    }

    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        (void)macro_ui_->manualOverrides.resume(automationAddress(index));
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationRecordingRevision.set(macro_ui_->automationRecordingRevision.get() + 1U);
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
