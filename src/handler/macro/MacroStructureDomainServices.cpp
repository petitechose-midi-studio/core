#include "handler/macro/MacroStructureDomainServices.hpp"

#include <array>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/macro/MacroAutomationClipboardOps.hpp"
#include "handler/macro/MacroStructureAutomationOps.hpp"
#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerStructureSelectionOps.hpp"
#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectControlStructureTransferOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;
namespace automation_clipboard_ops = core::handler::macro::automation_clipboard_ops;
namespace structure_automation_ops = core::handler::macro_structure_automation_ops;

namespace {

using StateRefs = MacroStructureDomainServices::StateRefs;
using Operations = MacroStructureDomainServices::Operations;

FLASHMEM void flushMutationCoalescing(Operations operations) {
    if (operations.flushMutationCoalescing != nullptr) {
        operations.flushMutationCoalescing(operations.context);
    }
}

FLASHMEM void markProjectMutated(Operations operations) {
    if (operations.markProjectMutated != nullptr) {
        operations.markProjectMutated(operations.context);
    }
}

FLASHMEM bool setSharedTrackState(Operations operations, uint16_t enabledMask, uint8_t activeTrack) {
    return operations.setSharedTrackState != nullptr &&
           operations.setSharedTrackState(operations.context, enabledMask, activeTrack);
}

FLASHMEM void syncActivePagePresentation(StateRefs state) {
    state.statusBar.pageName.set(state.pages.activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.macroUi.refreshManualOverrideMask(
        state.pages.currentActiveTrack(),
        state.pages.currentActivePage()
    );
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        float manualValue = 0.0f;
        if (state.macroUi.manualOverrides.valueFor(
                core::state::macro::MacroAutomationSlotAddress{
                    .track = state.pages.currentActiveTrack(),
                    .page = state.pages.currentActivePage(),
                    .macro = i,
                },
                manualValue
            )) {
            core::state::macro::MacroWorkflow::setRuntimeValue(
                state.macros,
                i,
                manualValue
            );
        }
    }
}

FLASHMEM void finalizeStructureChange(StateRefs state, Operations operations) {
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    syncActivePagePresentation(state);
    markProjectMutated(operations);
}

FLASHMEM void applyPageStructureMutation(StateRefs state,
                                         Operations operations,
                                         uint16_t enabledMask,
                                         uint8_t activePage) {
    flushMutationCoalescing(operations);
    state.pages.activeTrackData().enabledPageMask = enabledMask;
    state.pages.syncActiveTrackCache();
    state.pages.setActivePage(activePage);
    finalizeStructureChange(state, operations);
}

FLASHMEM void applyTrackStructureMutation(StateRefs state,
                                          Operations operations,
                                          uint16_t enabledMask,
                                          uint8_t activeTrack) {
    flushMutationCoalescing(operations);
    setSharedTrackState(operations, enabledMask, activeTrack);
    finalizeStructureChange(state, operations);
}

FLASHMEM void applyTrackStructureState(StateRefs state,
                                       Operations operations,
                                       uint16_t enabledMask,
    uint8_t activeTrack) {
    setSharedTrackState(operations, enabledMask, activeTrack);
    finalizeStructureChange(state, operations);
}

FLASHMEM void persistConfigChange(StateRefs state, Operations operations) {
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    markProjectMutated(operations);
}

FLASHMEM bool clearAutomationForPage(
    core::state::modulation::ProjectControlState& control,
    uint8_t track,
    uint8_t page
) {
    return structure_automation_ops::clearPages(
        control,
        track,
        structure_slots::slotBit(page)
    );
}

FLASHMEM bool clearAutomationForTrack(
    core::state::modulation::ProjectControlState& control,
    uint8_t track
) {
    return structure_automation_ops::clearTracks(
        control,
        structure_slots::slotBit(track)
    );
}

FLASHMEM void clearManualForPage(StateRefs state, uint8_t track, uint8_t page) {
    (void)state.macroUi.manualOverrides.clearPage(track, page);
}

FLASHMEM void clearManualForTrack(StateRefs state, uint8_t track) {
    (void)state.macroUi.manualOverrides.clearTrack(track);
}

FLASHMEM void clearManualForAddress(
    StateRefs state,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    (void)state.macroUi.manualOverrides.clearAddress(address);
}

FLASHMEM bool hasActiveProjectModulation(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address
) {
    const auto destination =
        core::state::modulation::projectControlDestination(address);
    for (uint16_t index = 0;
         index < control.authored.modulation.outputBindingCount;
         ++index) {
        const auto& binding = control.authored.modulation.outputBindings[index];
        if (binding.destination != destination ||
            (binding.flags &
             core::state::modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) == 0U) {
            continue;
        }
        const auto* source = core::state::modulation::findProjectModulator(
            control.authored.modulation,
            binding.sourceId
        );
        if (source != nullptr &&
            (source->flags &
             core::state::modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U) {
            return true;
        }
    }
    return false;
}

FLASHMEM void flushMutationCoalescingFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->flushProjectMutationCoalescing();
}

FLASHMEM void markProjectMutatedFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->markProjectMutated();
}

FLASHMEM bool setSharedTrackStateFromCoreState(void* context, uint16_t enabledMask, uint8_t activeTrack) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr && state->setSharedTrackState(enabledMask, activeTrack);
}

FLASHMEM void switchToPageFromCoreState(void* context, uint8_t pageIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToPage(*state, pageIndex);
}

FLASHMEM void switchToTrackFromCoreState(void* context, uint8_t trackIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToTrack(*state, trackIndex);
}

using MacroTrackStructureHistoryPtr =
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr;

FLASHMEM MacroTrackStructureHistoryPtr prepareMacroTrackStructureHistory(
    core::state::CoreState* state,
    const core::state::macro::MacroPagesState& pages,
    uint16_t trackMask
) {
    if (state == nullptr) return {};
    auto change = captureSequencerTrackStructureHistoryBefore(
        state->sequencerTracks,
        state->sequencer,
        trackMask
    );
    if (!change ||
        !core::state::sequencer::captureMacroTrackStructureHistoryBefore(
            pages,
            trackMask,
            *change
        )) {
        return {};
    }
    return change;
}

FLASHMEM bool rollbackMacroTrackStructureHistory(
    core::state::CoreState& coreState,
    StateRefs state,
    Operations operations,
    core::state::sequencer::SequencerHistoryTrackStructureChange& change
) {
    auto* macroStructure = change.macroStructure.get();
    if (macroStructure == nullptr) return false;
    if (!macroStructure->afterCaptured &&
        !core::state::sequencer::captureMacroTrackStructureHistoryAfter(
            state.pages,
            change
        )) {
        return false;
    }
    const bool macroRestored =
        core::state::sequencer::applyMacroTrackStructureHistory(
            state.pages,
            *macroStructure,
            false
        );
    const bool sequencerRestored =
        core::state::sequencer::applyHistoryStructureSnapshot(
            coreState.sequencerTracks,
            coreState.sequencer,
            change.before
        );
    (void)coreState.refreshSharedTrackStateFromSequencer();
    syncActivePagePresentation(state);
    return macroRestored && sequencerRestored;
}

FLASHMEM bool commitMacroTrackStructureHistory(
    core::state::CoreState* coreState,
    StateRefs state,
    Operations operations,
    MacroTrackStructureHistoryPtr change
) {
    if (coreState == nullptr) return true;
    if (!change) return false;

    const uint16_t historyMask = change->before.capturedTrackMask;
    if (!core::state::sequencer::captureMacroTrackStructureHistoryAfter(
            state.pages,
            *change
        ) ||
        !captureSequencerTrackStructureHistoryAfter(
            coreState->sequencerTracks,
            coreState->sequencer,
            historyMask,
            *change
        )) {
        (void)rollbackMacroTrackStructureHistory(
            *coreState,
            state,
            operations,
            *change
        );
        return false;
    }

    change->descriptor = makeSequencerTrackStructureHistoryDescriptor(
        change->before,
        change->after
    );
    const bool changed =
        !core::state::sequencer::sameMusicalHistoryStructureSnapshot(
            change->before,
            change->after
        ) ||
        core::state::sequencer::macroTrackStructureHistoryChanged(*change);
    if (!changed) return true;
    if (!coreState->sequencerHistory.canRecordStructure(*change)) {
        (void)rollbackMacroTrackStructureHistory(
            *coreState,
            state,
            operations,
            *change
        );
        return false;
    }
    coreState->sequencerHistory.recordPreparedStructure(std::move(change));
    return true;
}

}  // namespace

FLASHMEM MacroStructureDomainServices::MacroStructureDomainServices(
    StateRefs state,
    Operations operations
)
    : macros_(&state.macros)
    , pages_(&state.pages)
    , macro_ui_(&state.macroUi)
    , config_revision_(&state.configRevision)
    , status_bar_(&state.statusBar)
    , shared_track_active_(&state.sharedTrackActive)
    , shared_track_enabled_mask_(&state.sharedTrackEnabledMask)
    , history_(state.history)
    , core_state_(state.coreState)
    , operations_(operations) {}

FLASHMEM MacroStructureDomainServices MacroStructureDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return MacroStructureDomainServices{
        StateRefs{
            state.macros,
            state.pages,
            state.macroUi,
            state.configRevision,
            state.statusBar,
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
            &state.macroHistory,
            &state,
        },
        Operations{
            &state,
            flushMutationCoalescingFromCoreState,
            markProjectMutatedFromCoreState,
            setSharedTrackStateFromCoreState,
            switchToPageFromCoreState,
            switchToTrackFromCoreState,
        },
    };
}

FLASHMEM MacroStructureDomainServices::StateRefs MacroStructureDomainServices::stateRefs_() const {
    return StateRefs{
        *macros_,
        *pages_,
        *macro_ui_,
        *config_revision_,
        *status_bar_,
        *shared_track_active_,
        *shared_track_enabled_mask_,
        history_,
        core_state_,
    };
}

FLASHMEM void MacroStructureDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
        syncActivePagePresentation(stateRefs_());
    }
}

FLASHMEM void MacroStructureDomainServices::switchToTrack(uint8_t trackIndex) const {
    if (operations_.switchToTrack != nullptr) {
        operations_.switchToTrack(operations_.context, trackIndex);
        syncActivePagePresentation(stateRefs_());
    }
}

FLASHMEM uint8_t MacroStructureDomainServices::activeTrack() const {
    return shared_track_active_->get();
}

FLASHMEM uint16_t MacroStructureDomainServices::pageEnabledMask() const {
    return pages_->currentEnabledPageMask();
}

FLASHMEM uint16_t MacroStructureDomainServices::trackEnabledMask() const {
    return shared_track_enabled_mask_->get();
}

FLASHMEM bool MacroStructureDomainServices::deletePage(uint8_t pageIndex) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT || history_ == nullptr) {
        return false;
    }
    const uint16_t currentPageMask = pages_->currentEnabledPageMask();
    const uint16_t deletedPageBit = structure_slots::slotBit(pageIndex);
    if ((currentPageMask & deletedPageBit) == 0 ||
        structure_slots::countEnabled(
            currentPageMask,
            core::state::macro::PAGE_COUNT
        ) <= 1U) {
        return false;
    }

    const uint8_t trackIndex = pages_->currentActiveTrack();
    const uint16_t retainedPageMask = static_cast<uint16_t>(
        currentPageMask & static_cast<uint16_t>(~deletedPageBit)
    );

    flushMutationCoalescing(operations_);
    if (!history_->compactPages(*pages_, trackIndex, retainedPageMask)) {
        return false;
    }
    (void)macro_ui_->manualOverrides.compactPages(
        trackIndex,
        retainedPageMask
    );
    applyPageStructureMutation(
        stateRefs_(),
        operations_,
        pages_->tracks[trackIndex].enabledPageMask,
        pages_->tracks[trackIndex].activePage
    );
    return true;
}

FLASHMEM bool MacroStructureDomainServices::deleteActiveTrack() const {
    const uint8_t deletedTrack = activeTrack();
    const auto mutation = structure_slots::removeIndex(
        shared_track_enabled_mask_->get(),
        deletedTrack,
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    const uint16_t historyMask = static_cast<uint16_t>(
        structure_slots::slotBit(deletedTrack) |
        structure_slots::slotBit(mutation.nextActive)
    );
    auto trackHistory = prepareMacroTrackStructureHistory(
        core_state_,
        *pages_,
        historyMask
    );
    if (core_state_ != nullptr && !trackHistory) return false;

    flushMutationCoalescing(operations_);
    if (!clearAutomationForTrack(pages_->control, deletedTrack)) {
        if (trackHistory) {
            (void)rollbackMacroTrackStructureHistory(
                *core_state_, stateRefs_(), operations_, *trackHistory
            );
        }
        return false;
    }
    clearManualForTrack(stateRefs_(), deletedTrack);
    applyTrackStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return commitMacroTrackStructureHistory(
        core_state_, stateRefs_(), operations_, std::move(trackHistory)
    );
}

FLASHMEM bool MacroStructureDomainServices::erasePage(uint8_t pageIndex) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT || history_ == nullptr) {
        return false;
    }
    if (!pages_->activeTrackData().isPageEnabled(pageIndex)) return false;

    auto historyChange = history_->preparePageStructureSnapshot(
        *pages_,
        pages_->currentActiveTrack()
    );
    if (!historyChange) return false;

    flushMutationCoalescing(operations_);
    if (!clearAutomationForPage(
            pages_->control,
            pages_->currentActiveTrack(),
            pageIndex
        )) {
        return false;
    }
    pages_->activeTrackData().pages[pageIndex].initDefault(pageIndex);
    clearManualForPage(stateRefs_(), pages_->currentActiveTrack(), pageIndex);
    if (pages_->currentActivePage() == pageIndex) {
        pages_->setActivePage(pageIndex);
        syncActivePagePresentation(stateRefs_());
    }
    persistConfigChange(stateRefs_(), operations_);
    return history_->commitPreparedPageStructureSnapshot(
        *pages_,
        std::move(historyChange)
    );
}

FLASHMEM bool MacroStructureDomainServices::eraseTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if (!pages_->isTrackEnabled(trackIndex)) return false;

    const uint16_t historyMask = static_cast<uint16_t>(
        structure_slots::slotBit(activeTrack()) |
        structure_slots::slotBit(trackIndex)
    );
    auto trackHistory = prepareMacroTrackStructureHistory(
        core_state_,
        *pages_,
        historyMask
    );
    if (core_state_ != nullptr && !trackHistory) return false;

    flushMutationCoalescing(operations_);
    if (!clearAutomationForTrack(pages_->control, trackIndex)) {
        if (trackHistory) {
            (void)rollbackMacroTrackStructureHistory(
                *core_state_, stateRefs_(), operations_, *trackHistory
            );
        }
        return false;
    }
    pages_->tracks[trackIndex].initDefaults(trackIndex);
    clearManualForTrack(stateRefs_(), trackIndex);
    if (activeTrack() == trackIndex) {
        setSharedTrackState(operations_, trackEnabledMask(), trackIndex);
    }
    if (activeTrack() == trackIndex) {
        syncActivePagePresentation(stateRefs_());
    }
    persistConfigChange(stateRefs_(), operations_);
    return commitMacroTrackStructureHistory(
        core_state_, stateRefs_(), operations_, std::move(trackHistory)
    );
}

FLASHMEM bool MacroStructureDomainServices::pastePage(
    uint8_t pageIndex,
    const core::state::macro::MacroPageData& pageData,
    const core::state::MacroAutomationClipboard* automation
) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT || history_ == nullptr) {
        return false;
    }

    auto historyChange = history_->preparePageStructureSnapshot(
        *pages_,
        pages_->currentActiveTrack()
    );
    if (!historyChange) return false;

    flushMutationCoalescing(operations_);
    if (!structure_automation_ops::replacePageFromClipboard(
        pages_->control,
        pages_->currentActiveTrack(),
        pageIndex,
        automation
    )) {
        return false;
    }
    clearManualForPage(stateRefs_(), pages_->currentActiveTrack(), pageIndex);
    pages_->activeTrackData().pages[pageIndex] = pageData;
    pages_->activeTrackData().setPageEnabled(pageIndex, true);
    pages_->syncActiveTrackCache();
    pages_->setActivePage(pageIndex);
    finalizeStructureChange(stateRefs_(), operations_);
    return history_->commitPreparedPageStructureSnapshot(
        *pages_,
        std::move(historyChange)
    );
}

FLASHMEM bool MacroStructureDomainServices::pasteMacroPageSelection(
    const core::state::StructureClipboardState& clipboard,
    const core::state::MacroPageSelectionPastePlan& plan
) const {
    if (history_ == nullptr || !plan.canCommit() ||
        !clipboard.hasMacroPageSelection()) {
        return false;
    }
    const auto& source = *clipboard.macroPageSelection;
    const uint8_t track = pages_->currentActiveTrack();
    if (track >= core::state::macro::TRACK_COUNT ||
        source.sourceTrack >= core::state::macro::TRACK_COUNT ||
        plan.requiredPageCount == 0U ||
        plan.requiredPageCount > core::state::macro::PAGE_COUNT) {
        return false;
    }

    auto historyChange = history_->preparePageStructureSnapshot(
        *pages_,
        track
    );
    auto pendingControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >(pages_->control.authored);
    if (!historyChange || !pendingControl) return false;

    const uint8_t previousPageCount = static_cast<uint8_t>(
        structure_slots::countEnabled(
            pages_->tracks[track].enabledPageMask,
            core::state::macro::PAGE_COUNT
        )
    );
    if (plan.requiredPageCount > previousPageCount) {
        const uint16_t extensionMask = static_cast<uint16_t>(
            structure_slots::prefixMask(plan.requiredPageCount) &
            static_cast<uint16_t>(
                ~structure_slots::prefixMask(previousPageCount)
            )
        );
        if (extensionMask != 0U &&
            !structure_automation_ops::clearPagesInDomain(
                *pendingControl,
                track,
                extensionMask
            )) {
            return false;
        }
    }

    core::state::modulation::ProjectControlStructureTransferPlan
        transfer{};
    transfer.count = plan.count;
    for (uint8_t index = 0U; index < plan.count; ++index) {
        const auto& mapping = plan.entries[index];
        if (mapping.clipboardIndex >= source.count ||
            mapping.destinationPage >=
                core::state::macro::PAGE_COUNT) {
            return false;
        }
        const auto& sourcePage =
            source.pages[mapping.clipboardIndex];
        if (!sourcePage.valid) return false;
        transfer.entries[index] = {
            .sourceTrack = source.sourceTrack,
            .targetTrack = track,
            .sourcePage = sourcePage.sourcePage,
            .targetPage = mapping.destinationPage,
            .wholeTrack = false,
        };
    }
    if (!core::state::modulation::
            replaceProjectControlStructureInDomain(
                *pendingControl,
                *source.projectControl,
                transfer
            )) {
        return false;
    }

    auto pendingTrack = pages_->tracks[track];
    for (uint8_t page = previousPageCount;
         page < plan.requiredPageCount;
         ++page) {
        pendingTrack.pages[page].initDefault(page);
    }
    for (uint8_t index = 0U; index < plan.count; ++index) {
        const auto& mapping = plan.entries[index];
        pendingTrack.pages[mapping.destinationPage] =
            source.pages[mapping.clipboardIndex].page;
    }
    pendingTrack.enabledPageMask =
        structure_slots::prefixMask(plan.requiredPageCount);
    pendingTrack.activePage = plan.firstDestinationPage;

    flushMutationCoalescing(operations_);
    pages_->control.authored = *pendingControl;
    pages_->control.markAuthoredMutation();
    pages_->tracks[track] = pendingTrack;
    pages_->syncActiveTrackCache();
    pages_->setActivePage(plan.firstDestinationPage);
    for (uint8_t page = previousPageCount;
         page < plan.requiredPageCount;
         ++page) {
        clearManualForPage(stateRefs_(), track, page);
    }
    for (uint8_t index = 0U; index < plan.count; ++index) {
        clearManualForPage(
            stateRefs_(),
            track,
            plan.entries[index].destinationPage
        );
    }
    finalizeStructureChange(stateRefs_(), operations_);
    return history_->commitPreparedPageStructureSnapshot(
        *pages_,
        std::move(historyChange)
    );
}

FLASHMEM bool MacroStructureDomainServices::copyTrackSelection(
    uint16_t selectedMask,
    core::state::StructureClipboardState& clipboard
) const {
    if (core_state_ == nullptr) return false;
    auto payload = captureTrackSelectionClipboard(
        core_state_->sequencerTracks,
        core_state_->sequencer,
        core_state_->pages,
        selectedMask
    );
    return payload &&
        clipboard.storeSequencerTrackSelection(
            std::move(payload)
        );
}

FLASHMEM core::state::ClipboardTransferPlan
MacroStructureDomainServices::trackSelectionPastePlan(
    const core::state::StructureClipboardState& clipboard,
    uint8_t targetTrack
) const {
    if (core_state_ == nullptr) return {};
    return core::state::buildSequencerTrackClipboardTransferPlan(
        clipboard,
        core_state_->sequencerTracks,
        core_state_->projectTracks,
        targetTrack,
        core_state_->sequencerTrackActivations.pendingTrackMask()
    );
}

FLASHMEM bool MacroStructureDomainServices::pasteTrackSelection(
    const core::state::StructureClipboardState& clipboard,
    uint8_t targetTrack
) const {
    if (core_state_ == nullptr) return false;
    const auto result = executeSequencerTrackTransfer(
        core_state_->sequencerTracks,
        core_state_->projectTracks,
        core_state_->sequencer,
        clipboard,
        core::handler::SharedTrackDomainServices::fromCoreState(
            *core_state_
        ),
        core::handler::SequencerHistoryDomainServices::fromCoreState(
            *core_state_
        ),
        targetTrack,
        0U,
        &core_state_->sequencerTrackActivations,
        core_state_->statusBar.playing.get(),
        &core_state_->pages
    );
    return result.applied();
}

FLASHMEM bool MacroStructureDomainServices::pasteTrack(
    uint8_t trackIndex,
    const core::state::macro::MacroTrackData& trackData,
    const core::state::MacroAutomationClipboard* automation
) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;

    const uint16_t historyMask = static_cast<uint16_t>(
        structure_slots::slotBit(activeTrack()) |
        structure_slots::slotBit(trackIndex)
    );
    auto trackHistory = prepareMacroTrackStructureHistory(
        core_state_,
        *pages_,
        historyMask
    );
    if (core_state_ != nullptr && !trackHistory) return false;

    flushMutationCoalescing(operations_);
    if (!structure_automation_ops::replaceTrackFromClipboard(
            pages_->control,
            trackIndex,
            automation
        )) {
        if (trackHistory) {
            (void)rollbackMacroTrackStructureHistory(
                *core_state_, stateRefs_(), operations_, *trackHistory
            );
        }
        return false;
    }
    clearManualForTrack(stateRefs_(), trackIndex);
    pages_->tracks[trackIndex] = trackData;
    applyTrackStructureState(
        stateRefs_(),
        operations_,
        static_cast<uint16_t>(
            shared_track_enabled_mask_->get() |
            structure_slots::slotBit(trackIndex)
        ),
        trackIndex
    );
    return commitMacroTrackStructureHistory(
        core_state_, stateRefs_(), operations_, std::move(trackHistory)
    );
}

FLASHMEM bool MacroStructureDomainServices::createNextPage() const {
    if (history_ == nullptr) return false;
    const uint16_t enabledMask = pages_->currentEnabledPageMask();
    const int nextPage = structure_slots::nextAddIndexAfterHighest(
        enabledMask,
        core::state::macro::PAGE_COUNT
    );
    if (nextPage < 0) return false;

    const uint8_t index = static_cast<uint8_t>(nextPage);
    auto historyChange = history_->preparePageStructureSnapshot(
        *pages_,
        pages_->currentActiveTrack()
    );
    if (!historyChange) return false;
    flushMutationCoalescing(operations_);
    if (!clearAutomationForPage(
            pages_->control,
            pages_->currentActiveTrack(),
            index
        )) {
        return false;
    }
    pages_->activeTrackData().pages[index].initDefault(index);
    clearManualForPage(stateRefs_(), pages_->currentActiveTrack(), index);
    applyPageStructureMutation(
        stateRefs_(),
        operations_,
        static_cast<uint16_t>(enabledMask | structure_slots::slotBit(index)),
        index
    );
    return history_->commitPreparedPageStructureSnapshot(
        *pages_,
        std::move(historyChange)
    );
}

FLASHMEM bool MacroStructureDomainServices::createTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if ((shared_track_enabled_mask_->get() & structure_slots::slotBit(trackIndex)) != 0) {
        return false;
    }

    const uint16_t historyMask = static_cast<uint16_t>(
        sequencerStructureHistoryTrackBit(activeTrack()) |
        sequencerStructureHistoryTrackBit(trackIndex)
    );
    auto trackHistory = prepareMacroTrackStructureHistory(
        core_state_,
        *pages_,
        historyMask
    );
    if (core_state_ != nullptr && !trackHistory) return false;

    flushMutationCoalescing(operations_);
    if (!clearAutomationForTrack(pages_->control, trackIndex)) {
        if (trackHistory) {
            (void)rollbackMacroTrackStructureHistory(
                *core_state_, stateRefs_(), operations_, *trackHistory
            );
        }
        return false;
    }
    pages_->tracks[trackIndex].initDefaults(trackIndex);
    clearManualForTrack(stateRefs_(), trackIndex);
    applyTrackStructureMutation(
        stateRefs_(),
        operations_,
        static_cast<uint16_t>(
            shared_track_enabled_mask_->get() | structure_slots::slotBit(trackIndex)
        ),
        trackIndex
    );
    return commitMacroTrackStructureHistory(
        core_state_, stateRefs_(), operations_, std::move(trackHistory)
    );
}

FLASHMEM bool MacroStructureDomainServices::activateMacroSlot(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    if (pages_->activePageData().isMacroActive(index)) return true;
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::CREATE_SLOT
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!core::state::macro::MacroWorkflow::activateMacroSlot(*macros_, *pages_, index)) {
        return false;
    }
    if (history_ != nullptr &&
        !history_->commitPrepared(*pages_, std::move(change))) {
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
            *macros_,
            *pages_
        );
        return false;
    }

    if (config_revision_ != nullptr) {
        config_revision_->set(core::state::macro::nextMacroConfigRevision(
            config_revision_->get(),
            index
        ));
    }
    markProjectMutated(operations_);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::clearMacroAutomation(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT || history_ == nullptr) {
        return false;
    }
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages_->control,
            address,
            slot
        ) || !slot.automation.stored()) {
        return false;
    }

    auto change = history_->prepare(
        *pages_,
        address,
        core::state::macro::MacroHistoryActionKind::CLEAR_AUTOMATION
    );
    if (!change) return false;
    flushMutationCoalescing(operations_);
    if (!core::state::modulation::clearProjectControlAutomation(
            pages_->control,
            address
        )) {
        return false;
    }
    if (!history_->commitPrepared(*pages_, std::move(change))) {
        return false;
    }
    if (!hasActiveProjectModulation(pages_->control, address)) {
        clearManualForAddress(stateRefs_(), address);
    }
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
    persistConfigChange(stateRefs_(), operations_);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::removeMacroAutomation(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT || history_ == nullptr) {
        return false;
    }
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
    if (!pages_->isMacroSlotActive(index)) return false;
    flushMutationCoalescing(operations_);
    if (!history_->removeMacroSlot(*pages_, address)) {
        return false;
    }

    clearManualForAddress(stateRefs_(), address);
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, 0.5f);
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
    persistConfigChange(stateRefs_(), operations_);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::copyMacroAutomation(
    uint8_t index,
    core::state::StructureClipboardState& clipboard
) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;

    return automation_clipboard_ops::copySlotToClipboard(
        *pages_,
        core::state::macro::MacroAutomationSlotAddress{
            .track = pages_->currentActiveTrack(),
            .page = pages_->currentActivePage(),
            .macro = index,
        },
        clipboard
    );
}

FLASHMEM bool MacroStructureDomainServices::pasteMacroAutomation(
    uint8_t index,
    const core::state::StructureClipboardState& clipboard
) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
    const auto plan = automation_clipboard_ops::preflightSlotPaste(
        *pages_,
        address,
        clipboard
    );
    if (!plan.actionable()) return false;

    flushMutationCoalescing(operations_);
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
            clipboard,
            true
        )) {
        if (change) {
            (void)core::state::macro::applyMacroSlotHistorySnapshot(
                *pages_,
                change->slot->before
            );
        }
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(
            *pages_,
            std::move(change)
        )) {
        return false;
    }

    clearManualForAddress(stateRefs_(), address);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
    persistConfigChange(stateRefs_(), operations_);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::pasteMacroSlotSelection(
    const core::state::StructureClipboardState& clipboard,
    const core::state::macro::MacroSlotClipboardPlan& plan
) const {
    if (history_ == nullptr || !plan.canCommit() ||
        plan.targetTrack != pages_->currentActiveTrack() ||
        plan.clipboardRevision != clipboard.revision.get()) {
        return false;
    }
    const auto livePlan =
        core::state::macro::buildMacroSlotClipboardPlan(
            clipboard,
            *pages_,
            plan.targetTrack,
            plan.anchorLinear
        );
    if (!livePlan.canCommit() ||
        !core::state::macro::sameMacroSlotClipboardPlan(
            plan,
            livePlan
        )) {
        return false;
    }

    auto historyChange = history_->preparePageStructureSnapshot(
        *pages_,
        plan.targetTrack
    );
    if (!historyChange || !historyChange->pageStructure ||
        !historyChange->pageStructure->beforeControl ||
        !historyChange->pageStructure->afterControl) {
        return false;
    }
    auto& historyPayload = *historyChange->pageStructure;
    auto pendingTrack = pages_->tracks[plan.targetTrack];
    auto& pendingDomain = *historyPayload.afterControl;
    pendingDomain = pages_->control.authored;

    flushMutationCoalescing(operations_);
    if (plan.createPageMask != 0U) {
        if (!structure_automation_ops::clearPagesInDomain(
                pendingDomain,
                plan.targetTrack,
                plan.createPageMask
            )) {
            return false;
        }
        for (uint8_t page = plan.existingPageCount;
             page < plan.requiredPageCount;
             ++page) {
            pendingTrack.pages[page].initDefault(page);
            pendingTrack.pages[page].activeMacroMask = 0U;
            pendingTrack.setPageEnabled(page, true);
        }
    }

    for (uint8_t index = 0U; index < plan.count; ++index) {
        const auto& entry = plan.entries[index];
        if (!automation_clipboard_ops::
                pasteSlotEntryFromClipboardInDomain(
                    pendingDomain,
                    pendingTrack.pages[entry.targetPage],
                    core::state::macro::MacroAutomationSlotAddress{
                        .track = plan.targetTrack,
                        .page = entry.targetPage,
                        .macro = entry.targetMacro,
                    },
                    clipboard,
                    entry.clipboardIndex
                )) {
            return false;
        }
    }
    if (!core::state::modulation::validProjectModulationDomain(
            pendingDomain.modulation,
            pendingDomain.curves,
            &pendingDomain.automation
        )) {
        return false;
    }

    pendingTrack.activePage = static_cast<uint8_t>(
        plan.anchorLinear / core::state::macro::MACRO_COUNT
    );
    const bool trackChanged =
        std::memcmp(
            &historyPayload.beforeTrack,
            &pendingTrack,
            sizeof(pendingTrack)
        ) != 0;
    const bool controlChanged =
        std::memcmp(
            historyPayload.beforeControl.get(),
            &pendingDomain,
            sizeof(pendingDomain)
        ) != 0;
    if (!trackChanged && !controlChanged) return true;

    pages_->control.authored = pendingDomain;
    pages_->control.markAuthoredMutation();
    pages_->tracks[plan.targetTrack] = pendingTrack;
    pages_->syncActiveTrackCache();
    pages_->setActivePage(pendingTrack.activePage);

    for (uint8_t page = plan.existingPageCount;
         page < plan.requiredPageCount;
         ++page) {
        clearManualForPage(stateRefs_(), plan.targetTrack, page);
    }
    for (uint8_t index = 0U; index < plan.count; ++index) {
        const auto& entry = plan.entries[index];
        clearManualForAddress(
            stateRefs_(),
            core::state::macro::MacroAutomationSlotAddress{
                .track = plan.targetTrack,
                .page = entry.targetPage,
                .macro = entry.targetMacro,
            }
        );
    }
    finalizeStructureChange(stateRefs_(), operations_);
    return history_->commitPreparedPageStructureSnapshot(
        *pages_,
        std::move(historyChange)
    );
}

}  // namespace core::handler
