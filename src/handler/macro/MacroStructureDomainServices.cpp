#include "handler/macro/MacroStructureDomainServices.hpp"

#include <array>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/macro/MacroAutomationClipboardOps.hpp"
#include "handler/macro/MacroStructureAutomationOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

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

FLASHMEM bool MacroStructureDomainServices::deleteActivePage() const {
    const auto mutation = structure_slots::removeIndex(
        pages_->currentEnabledPageMask(),
        pages_->currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    flushMutationCoalescing(operations_);
    if (!clearAutomationForPage(
        pages_->control,
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    )) return false;
    clearManualForPage(
        stateRefs_(),
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
    applyPageStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::deleteActiveTrack() const {
    const auto mutation = structure_slots::removeIndex(
        shared_track_enabled_mask_->get(),
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    flushMutationCoalescing(operations_);
    if (!clearAutomationForTrack(pages_->control, activeTrack())) return false;
    clearManualForTrack(stateRefs_(), activeTrack());
    applyTrackStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::deleteSelectedPages(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        pages_->currentEnabledPageMask(),
        selectedMask,
        pages_->currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    const uint16_t deleteMask = pages_->currentEnabledPageMask() & selectedMask;
    flushMutationCoalescing(operations_);
    if (!structure_automation_ops::clearPages(
            pages_->control,
            pages_->currentActiveTrack(),
            deleteMask
        )) {
        return false;
    }
    for (uint8_t page = 0; page < core::state::macro::PAGE_COUNT; ++page) {
        if ((deleteMask & structure_slots::slotBit(page)) != 0) {
            clearManualForPage(stateRefs_(), pages_->currentActiveTrack(), page);
        }
    }
    applyPageStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::deleteSelectedTracks(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        shared_track_enabled_mask_->get(),
        selectedMask,
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    const uint16_t deleteMask = shared_track_enabled_mask_->get() & selectedMask;
    flushMutationCoalescing(operations_);
    if (!structure_automation_ops::clearTracks(pages_->control, deleteMask)) {
        return false;
    }
    for (uint8_t track = 0; track < core::state::macro::TRACK_COUNT; ++track) {
        if ((deleteMask & structure_slots::slotBit(track)) != 0) {
            clearManualForTrack(stateRefs_(), track);
        }
    }
    applyTrackStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::duplicateSelectedPages(uint16_t selectedMask) const {
    std::array<
        structure_automation_ops::ProjectControlPageCopy,
        core::state::macro::PAGE_COUNT
    > copies{};
    uint8_t copyCount = 0;
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        pages_->currentEnabledPageMask(),
        selectedMask,
        core::state::macro::PAGE_COUNT,
        [this, &copies, &copyCount](uint8_t source, uint8_t dest) {
            if (copyCount >= copies.size()) return false;
            copies[copyCount++] = {
                .sourceTrack = pages_->currentActiveTrack(),
                .sourcePage = source,
                .destTrack = pages_->currentActiveTrack(),
                .destPage = dest,
            };
            return true;
        }
    );
    if (!result.changed) return false;
    flushMutationCoalescing(operations_);
    if (!structure_automation_ops::duplicatePages(
            pages_->control,
            copies.data(),
            copyCount
        )) {
        return false;
    }
    for (uint8_t index = 0; index < copyCount; ++index) {
        const auto& copy = copies[index];
        clearManualForPage(stateRefs_(), copy.destTrack, copy.destPage);
        pages_->tracks[copy.destTrack].pages[copy.destPage] =
            pages_->tracks[copy.sourceTrack].pages[copy.sourcePage];
    }

    applyPageStructureMutation(
        stateRefs_(),
        operations_,
        result.nextMask,
        result.firstDuplicated < core::state::macro::PAGE_COUNT
            ? result.firstDuplicated
            : pages_->currentActivePage()
    );
    return true;
}

FLASHMEM bool MacroStructureDomainServices::duplicateSelectedTracks(uint16_t selectedMask) const {
    std::array<
        structure_automation_ops::ProjectControlTrackCopy,
        core::state::macro::TRACK_COUNT
    > copies{};
    uint8_t copyCount = 0;
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        shared_track_enabled_mask_->get(),
        selectedMask,
        core::state::macro::TRACK_COUNT,
        [&copies, &copyCount](uint8_t source, uint8_t dest) {
            if (copyCount >= copies.size()) return false;
            copies[copyCount++] = {
                .sourceTrack = source,
                .destTrack = dest,
            };
            return true;
        }
    );
    if (!result.changed) return false;
    flushMutationCoalescing(operations_);
    if (!structure_automation_ops::duplicateTracks(
            pages_->control,
            copies.data(),
            copyCount
        )) {
        return false;
    }
    for (uint8_t index = 0; index < copyCount; ++index) {
        const auto& copy = copies[index];
        clearManualForTrack(stateRefs_(), copy.destTrack);
        pages_->tracks[copy.destTrack] = pages_->tracks[copy.sourceTrack];
    }

    applyTrackStructureMutation(
        stateRefs_(),
        operations_,
        result.nextMask,
        result.firstDuplicated < core::state::macro::TRACK_COUNT
            ? result.firstDuplicated
            : activeTrack()
    );
    return true;
}

FLASHMEM bool MacroStructureDomainServices::erasePage(uint8_t pageIndex) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;
    if (!pages_->activeTrackData().isPageEnabled(pageIndex)) return false;

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
    return true;
}

FLASHMEM bool MacroStructureDomainServices::eraseTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if (!pages_->isTrackEnabled(trackIndex)) return false;

    flushMutationCoalescing(operations_);
    if (!clearAutomationForTrack(pages_->control, trackIndex)) return false;
    pages_->tracks[trackIndex].initDefaults(trackIndex);
    clearManualForTrack(stateRefs_(), trackIndex);
    if (activeTrack() == trackIndex) {
        setSharedTrackState(operations_, trackEnabledMask(), trackIndex);
        syncActivePagePresentation(stateRefs_());
    }
    persistConfigChange(stateRefs_(), operations_);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::pastePage(
    uint8_t pageIndex,
    const core::state::macro::MacroPageData& pageData,
    const core::state::MacroAutomationClipboard* automation
) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;

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
    return true;
}

FLASHMEM bool MacroStructureDomainServices::pasteTrack(
    uint8_t trackIndex,
    const core::state::macro::MacroTrackData& trackData,
    const core::state::MacroAutomationClipboard* automation
) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;

    flushMutationCoalescing(operations_);
    if (!structure_automation_ops::replaceTrackFromClipboard(
            pages_->control,
            trackIndex,
            automation
        )) {
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
    return true;
}

FLASHMEM bool MacroStructureDomainServices::createNextPage() const {
    const uint16_t enabledMask = pages_->currentEnabledPageMask();
    const int nextPage = structure_slots::nextAddIndexAfterHighest(
        enabledMask,
        core::state::macro::PAGE_COUNT
    );
    if (nextPage < 0) return false;

    const uint8_t index = static_cast<uint8_t>(nextPage);
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
    return true;
}

FLASHMEM bool MacroStructureDomainServices::createTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if ((shared_track_enabled_mask_->get() & structure_slots::slotBit(trackIndex)) != 0) {
        return false;
    }

    flushMutationCoalescing(operations_);
    if (!clearAutomationForTrack(pages_->control, trackIndex)) return false;
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
    return true;
}

FLASHMEM bool MacroStructureDomainServices::activateMacroSlot(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    if (!core::state::macro::MacroWorkflow::activateMacroSlot(*macros_, *pages_, index)) {
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

FLASHMEM bool MacroStructureDomainServices::macroAutomationActive(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    return pages_->isMacroSlotActive(index);
}

FLASHMEM bool MacroStructureDomainServices::clearMacroAutomation(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
    core::state::modulation::ProjectControlMacroSlotView slot{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages_->control,
            address,
            slot
        ) || !slot.automationStored) {
        return false;
    }

    flushMutationCoalescing(operations_);
    if (!core::state::modulation::clearProjectControlAutomation(
            pages_->control,
            address
        )) {
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

}  // namespace core::handler
