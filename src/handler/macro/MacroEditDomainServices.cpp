#include "handler/macro/MacroEditDomainServices.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/macro/MacroAutomationClipboardOps.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

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
    const core::state::modulation::ProjectControlMacroDestinationView* view
) {
    return view != nullptr &&
           ((view->automation.stored() && view->automation.enabled) ||
            view->activeModulationCount > 0U);
}

}  // namespace

FLASHMEM MacroEditDomainServices::MacroEditDomainServices(StateRefs state,
                                                          Operations operations)
    : pages_(&state.pages)
    , project_tracks_(&state.projectTracks)
    , macro_ui_(state.macroUi)
    , clipboard_(state.clipboard)
    , macros_(state.macros)
    , history_(state.history)
    , operations_(operations) {}

FLASHMEM MacroEditDomainServices MacroEditDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return MacroEditDomainServices{
        StateRefs{
            state.pages,
            state.projectTracks,
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

FLASHMEM bool MacroEditDomainServices::synchronizeSharedTrackState() const {
    return pages_ != nullptr &&
           operations_.synchronizeSharedTrackState != nullptr &&
           operations_.synchronizeSharedTrackState(
               operations_.context,
               pages_->currentTrackEnabledMask(),
               pages_->currentActiveTrack()
           );
}

FLASHMEM const core::state::macro::MacroConfig&
MacroEditDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(*pages_, index);
}

FLASHMEM bool MacroEditDomainServices::isMacroSlotActive(uint8_t index) const {
    return pages_ != nullptr && pages_->isMacroSlotActive(index);
}

FLASHMEM bool MacroEditDomainServices::setConfig(uint8_t index,
                                                uint8_t channel,
                                                uint8_t cc) const {
    return operations_.setConfig != nullptr &&
           operations_.setConfig(operations_.context, index, channel, cc);
}

FLASHMEM void MacroEditDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
    }
}

FLASHMEM void MacroEditDomainServices::switchToTrack(uint8_t trackIndex) const {
    if (operations_.switchToTrack != nullptr) {
        operations_.switchToTrack(operations_.context, trackIndex);
    }
}

FLASHMEM core::state::macro::MacroAutomationSlotAddress
MacroEditDomainServices::automationAddress(uint8_t index) const {
    return core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
}

FLASHMEM const core::state::modulation::ProjectControlMacroDestinationView*
MacroEditDomainServices::controlDestination(uint8_t index) const {
    if (pages_ == nullptr ||
        !core::state::modulation::readProjectControlMacroDestination(
            pages_->control,
            automationAddress(index),
            destination_view_cache_
        ) || !destination_view_cache_.present()) {
        return nullptr;
    }
    return &destination_view_cache_;
}

FLASHMEM bool MacroEditDomainServices::automationActiveFor(uint8_t index) const {
    const auto* view = controlDestination(index);
    return view != nullptr && view->automation.stored();
}

FLASHMEM bool MacroEditDomainServices::automationStoredFor(uint8_t index) const {
    const auto* view = controlDestination(index);
    return view != nullptr && view->automation.stored();
}

FLASHMEM bool MacroEditDomainServices::automationPlaybackActiveFor(
    uint8_t index
) const {
    const auto* view = controlDestination(index);
    return view != nullptr && view->automation.stored() &&
           view->automation.enabled;
}

FLASHMEM bool MacroEditDomainServices::manualOverrideActiveFor(
    uint8_t index
) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return false;
    return macro_ui_->manualOverrides.activeFor(automationAddress(index));
}

FLASHMEM void MacroEditDomainServices::setManualOverride(uint8_t index,
                                                        bool active) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return;
    const auto address = automationAddress(index);
    if (active) {
        const auto* view = controlDestination(index);
        if (!computedSourcePlaybackActive(view)) return;
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

FLASHMEM bool MacroEditDomainServices::setAutomationPlayback(
    uint8_t index,
    bool active
) const {
    const auto address = automationAddress(index);
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages_->control,
            address,
            view
        ) || !view.automation.stored() || view.automation.enabled == active) {
        return false;
    }

    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::AUTOMATION_STATE
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
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
        );
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::clearAutomation(uint8_t index) const {
    const auto address = automationAddress(index);
    core::state::modulation::ProjectControlMacroDestinationView before{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages_->control,
            address,
            before
        ) || !before.automation.stored()) {
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
        core::state::modulation::ProjectControlMacroDestinationView after{};
        const bool hasComputedSource =
            core::state::modulation::readProjectControlMacroDestination(
                pages_->control,
                address,
                after
            ) && after.present() &&
            ((after.automation.stored() && after.automation.enabled) ||
             after.activeModulationCount > 0U);
        if (!hasComputedSource) {
            (void)macro_ui_->manualOverrides.resume(address);
            macro_ui_->refreshManualOverrideMask(
                pages_->currentActiveTrack(),
                pages_->currentActivePage()
            );
        }
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
        );
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

FLASHMEM automation_clipboard_ops::MacroTypedPastePreflight
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
    const uint8_t channel = core::state::project::projectTrackMidiChannel(
        *project_tracks_,
        pages_->currentActiveTrack()
    );
    if (!setConfig(index, channel, cc)) return false;
    if (history_ != nullptr &&
        !history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    return true;
}

FLASHMEM automation_clipboard_ops::MacroTypedPastePreflight
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
            macro_ui_->automationEditRevision.set(
                macro_ui_->automationEditRevision.get() + 1U
            );
        }
    }
    if (projectChanged && operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return projectChanged || manualChanged;
}

FLASHMEM core::state::modulation::ProjectAutomationConversionPlan
MacroEditDomainServices::preflightConversion(
    uint8_t index,
    core::state::modulation::ProjectAutomationConversionPolicy policy
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
    const core::state::modulation::ProjectAutomationConversionPlan& plan,
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
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
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

FLASHMEM bool MacroEditDomainServices::resumeSources(uint8_t index) const {
    if (macro_ui_ == nullptr || index >= core::state::macro::MACRO_COUNT) return false;
    const auto address = automationAddress(index);
    const bool hadManual = macro_ui_->manualOverrides.activeFor(address);
    if (!hadManual) return false;
    (void)macro_ui_->manualOverrides.resume(address);
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
    macro_ui_->automationEditRevision.set(
        macro_ui_->automationEditRevision.get() + 1U
    );
    return true;
}

FLASHMEM bool MacroEditDomainServices::deleteSlot(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT ||
        !pages_->isMacroSlotActive(index) || history_ == nullptr) {
        return false;
    }
    const auto address = automationAddress(index);
    if (!history_->deleteMacroSlot(*pages_, address)) {
        return false;
    }
    if (macro_ui_ != nullptr) {
        (void)macro_ui_->manualOverrides.resume(address);
        macro_ui_->refreshManualOverrideMask(
            pages_->currentActiveTrack(),
            pages_->currentActivePage()
        );
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
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

FLASHMEM bool MacroEditDomainServices::setAutomationDurationBeats(
    uint8_t index,
    float durationBeats
) const {
    if (history_ == nullptr || index >= core::state::macro::MACRO_COUNT) {
        return false;
    }
    const auto address = automationAddress(index);
    if (!history_->setAutomationDurationBeatsCoalesced(
            *pages_,
            address,
            durationBeats
        )) {
        return false;
    }

    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM bool MacroEditDomainServices::setAutomationWindowOffsetBeats(
    uint8_t index,
    float offsetBeats
) const {
    if (history_ == nullptr || index >= core::state::macro::MACRO_COUNT) {
        return false;
    }
    const auto address = automationAddress(index);
    if (!history_->setAutomationWindowOffsetBeatsCoalesced(
            *pages_,
            address,
            offsetBeats
        )) {
        return false;
    }

    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    if (macro_ui_ != nullptr) {
        macro_ui_->automationEditRevision.set(
            macro_ui_->automationEditRevision.get() + 1U
        );
    }
    return true;
}

FLASHMEM void MacroEditDomainServices::markProjectMutated() const {
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
}

}  // namespace core::handler
