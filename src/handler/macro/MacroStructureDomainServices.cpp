#include "handler/macro/MacroStructureDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

using StateRefs = MacroStructureDomainServices::StateRefs;
using Operations = MacroStructureDomainServices::Operations;

FLASHMEM void flushAutoPersist(Operations operations) {
    if (operations.flushAutoPersist != nullptr) {
        operations.flushAutoPersist(operations.context);
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
    flushAutoPersist(operations);
    state.pages.activeTrackData().enabledPageMask = enabledMask;
    state.pages.syncActiveTrackCache();
    state.pages.setActivePage(activePage);
    finalizeStructureChange(state, operations);
}

FLASHMEM void applyTrackStructureMutation(StateRefs state,
                                          Operations operations,
                                          uint16_t enabledMask,
                                          uint8_t activeTrack) {
    flushAutoPersist(operations);
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

FLASHMEM void clearAutomationForPage(core::state::macro::MacroAutomationBankState& bank,
                                     uint8_t track,
                                     uint8_t page) {
    core::state::macro::macroAutomationClearPage(bank, track, page);
}

FLASHMEM void clearAutomationForTrack(core::state::macro::MacroAutomationBankState& bank,
                                      uint8_t track) {
    core::state::macro::macroAutomationClearTrack(bank, track);
}

FLASHMEM void copyAutomationSlot(core::state::macro::MacroAutomationBankState& bank,
                                 uint8_t sourceTrack,
                                 uint8_t sourcePage,
                                 uint8_t sourceMacro,
                                 uint8_t destTrack,
                                 uint8_t destPage,
                                 uint8_t destMacro) {
    const auto* source = core::state::macro::macroAutomationFindSlot(
        bank,
        core::state::macro::MacroAutomationSlotAddress{
            .track = sourceTrack,
            .page = sourcePage,
            .macro = sourceMacro,
        }
    );
    if (source == nullptr) return;
    auto* dest = core::state::macro::macroAutomationGetOrCreateSlot(
        bank,
        core::state::macro::MacroAutomationSlotAddress{
            .track = destTrack,
            .page = destPage,
            .macro = destMacro,
        }
    );
    if (dest == nullptr) return;
    core::state::macro::macroAutomationCopySlotState(
        bank,
        *dest,
        bank.pointPool,
        *source
    );
}

FLASHMEM void copyAutomationPage(core::state::macro::MacroAutomationBankState& bank,
                                 uint8_t sourceTrack,
                                 uint8_t sourcePage,
                                 uint8_t destTrack,
                                 uint8_t destPage) {
    clearAutomationForPage(bank, destTrack, destPage);
    for (uint8_t macro = 0; macro < core::state::macro::MACRO_COUNT; ++macro) {
        copyAutomationSlot(bank, sourceTrack, sourcePage, macro, destTrack, destPage, macro);
    }
}

FLASHMEM void copyAutomationTrack(core::state::macro::MacroAutomationBankState& bank,
                                  uint8_t sourceTrack,
                                  uint8_t destTrack) {
    clearAutomationForTrack(bank, destTrack);
    for (uint8_t page = 0; page < core::state::macro::PAGE_COUNT; ++page) {
        for (uint8_t macro = 0; macro < core::state::macro::MACRO_COUNT; ++macro) {
            copyAutomationSlot(bank, sourceTrack, page, macro, destTrack, page, macro);
        }
    }
}

FLASHMEM void applyAutomationClipboardToPage(
    core::state::macro::MacroAutomationBankState& bank,
    uint8_t destTrack,
    uint8_t destPage,
    const core::state::MacroAutomationClipboard* clipboard
) {
    clearAutomationForPage(bank, destTrack, destPage);
    if (clipboard == nullptr || !clipboard->valid || clipboard->trackScope) return;
    const uint8_t count = clipboard->count >
            core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY
        ? core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY
        : clipboard->count;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = clipboard->entries[i];
        if (!entry.valid || entry.sourceMacro >= core::state::macro::MACRO_COUNT) continue;
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            bank,
            core::state::macro::MacroAutomationSlotAddress{
                .track = destTrack,
                .page = destPage,
                .macro = entry.sourceMacro,
            }
        );
        if (slot == nullptr) continue;
        core::state::macro::macroAutomationCopySlotState(
            bank,
            *slot,
            clipboard->pointPool,
            entry.state
        );
    }
}

FLASHMEM void applyAutomationClipboardToTrack(
    core::state::macro::MacroAutomationBankState& bank,
    uint8_t destTrack,
    const core::state::MacroAutomationClipboard* clipboard
) {
    clearAutomationForTrack(bank, destTrack);
    if (clipboard == nullptr || !clipboard->valid || !clipboard->trackScope) return;
    const uint8_t count = clipboard->count >
            core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY
        ? core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY
        : clipboard->count;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = clipboard->entries[i];
        if (!entry.valid ||
            entry.sourcePage >= core::state::macro::PAGE_COUNT ||
            entry.sourceMacro >= core::state::macro::MACRO_COUNT) {
            continue;
        }
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            bank,
            core::state::macro::MacroAutomationSlotAddress{
                .track = destTrack,
                .page = entry.sourcePage,
                .macro = entry.sourceMacro,
            }
        );
        if (slot == nullptr) continue;
        core::state::macro::macroAutomationCopySlotState(
            bank,
            *slot,
            clipboard->pointPool,
            entry.state
        );
    }
}

FLASHMEM void flushAutoPersistFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->flushAutoPersist();
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
    , config_revision_(&state.configRevision)
    , status_bar_(&state.statusBar)
    , shared_track_active_(&state.sharedTrackActive)
    , shared_track_enabled_mask_(&state.sharedTrackEnabledMask)
    , operations_(operations) {}

FLASHMEM MacroStructureDomainServices MacroStructureDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return MacroStructureDomainServices{
        StateRefs{
            state.macros,
            state.pages,
            state.configRevision,
            state.statusBar,
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
        },
        Operations{
            &state,
            flushAutoPersistFromCoreState,
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
        *config_revision_,
        *status_bar_,
        *shared_track_active_,
        *shared_track_enabled_mask_,
    };
}

FLASHMEM void MacroStructureDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
    }
}

FLASHMEM void MacroStructureDomainServices::switchToTrack(uint8_t trackIndex) const {
    if (operations_.switchToTrack != nullptr) {
        operations_.switchToTrack(operations_.context, trackIndex);
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

    clearAutomationForPage(
        pages_->automation,
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

    clearAutomationForTrack(pages_->automation, activeTrack());
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
    for (uint8_t page = 0; page < core::state::macro::PAGE_COUNT; ++page) {
        if ((deleteMask & structure_slots::slotBit(page)) != 0) {
            clearAutomationForPage(pages_->automation, pages_->currentActiveTrack(), page);
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
    for (uint8_t track = 0; track < core::state::macro::TRACK_COUNT; ++track) {
        if ((deleteMask & structure_slots::slotBit(track)) != 0) {
            clearAutomationForTrack(pages_->automation, track);
        }
    }
    applyTrackStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::duplicateSelectedPages(uint16_t selectedMask) const {
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        pages_->currentEnabledPageMask(),
        selectedMask,
        core::state::macro::PAGE_COUNT,
        [this](uint8_t source, uint8_t dest) {
            pages_->activeTrackData().pages[dest] = pages_->activeTrackData().pages[source];
            copyAutomationPage(
                pages_->automation,
                pages_->currentActiveTrack(),
                source,
                pages_->currentActiveTrack(),
                dest
            );
        }
    );
    if (!result.changed) return false;

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
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        shared_track_enabled_mask_->get(),
        selectedMask,
        core::state::macro::TRACK_COUNT,
        [this](uint8_t source, uint8_t dest) {
            pages_->tracks[dest] = pages_->tracks[source];
            copyAutomationTrack(pages_->automation, source, dest);
        }
    );
    if (!result.changed) return false;

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

    flushAutoPersist(operations_);
    pages_->activeTrackData().pages[pageIndex].initDefault(pageIndex);
    clearAutomationForPage(pages_->automation, pages_->currentActiveTrack(), pageIndex);
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

    flushAutoPersist(operations_);
    pages_->tracks[trackIndex].initDefaults(trackIndex);
    clearAutomationForTrack(pages_->automation, trackIndex);
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

    flushAutoPersist(operations_);
    pages_->activeTrackData().pages[pageIndex] = pageData;
    applyAutomationClipboardToPage(
        pages_->automation,
        pages_->currentActiveTrack(),
        pageIndex,
        automation
    );
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

    flushAutoPersist(operations_);
    pages_->tracks[trackIndex] = trackData;
    applyAutomationClipboardToTrack(pages_->automation, trackIndex, automation);
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
    pages_->activeTrackData().pages[index].initDefault(index);
    clearAutomationForPage(pages_->automation, pages_->currentActiveTrack(), index);
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

    pages_->tracks[trackIndex].initDefaults(trackIndex);
    clearAutomationForTrack(pages_->automation, trackIndex);
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
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        pages_->automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = pages_->currentActiveTrack(),
            .page = pages_->currentActivePage(),
            .macro = index,
        }
    );
    return slot != nullptr && slot->automation.active;
}

FLASHMEM bool MacroStructureDomainServices::clearMacroAutomation(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        pages_->automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = pages_->currentActiveTrack(),
            .page = pages_->currentActivePage(),
            .macro = index,
        }
    );
    if (slot == nullptr || !slot->automation.active) return false;

    flushAutoPersist(operations_);
    core::state::macro::macroAutomationClearAutomation(pages_->automation, *slot);
    persistConfigChange(stateRefs_(), operations_);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::removeMacroAutomation(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    flushAutoPersist(operations_);
    const bool removed = core::state::macro::macroAutomationClearSlot(
        pages_->automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = pages_->currentActiveTrack(),
            .page = pages_->currentActivePage(),
            .macro = index,
        }
    );
    if (!removed) return false;

    persistConfigChange(stateRefs_(), operations_);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::copyMacroAutomation(
    uint8_t index,
    core::state::StructureClipboardState& clipboard
) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        pages_->automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = pages_->currentActiveTrack(),
            .page = pages_->currentActivePage(),
            .macro = index,
        }
    );
    if (slot == nullptr || !slot->automation.active) return false;

    clipboard.storeMacroAutomation(pages_->automation, *slot);
    return true;
}

FLASHMEM bool MacroStructureDomainServices::pasteMacroAutomation(
    uint8_t index,
    const core::state::StructureClipboardState& clipboard
) const {
    if (index >= core::state::macro::MACRO_COUNT || !clipboard.hasMacroAutomation()) {
        return false;
    }
    if (!clipboard.macroAutomationSet ||
        !clipboard.macroAutomationSet->valid ||
        clipboard.macroAutomationSet->count == 0) {
        return false;
    }

    const auto& entry = clipboard.macroAutomationSet->entries[0];
    if (!entry.valid || !entry.state.automation.active) return false;

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
    const bool hadSlot = core::state::macro::macroAutomationFindSlot(
        pages_->automation,
        address
    ) != nullptr;
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        pages_->automation,
        address
    );
    if (slot == nullptr) return false;

    flushAutoPersist(operations_);
    if (!core::state::macro::macroAutomationCopySlotState(
            pages_->automation,
            *slot,
            clipboard.macroAutomationSet->pointPool,
            entry.state
        )) {
        if (!hadSlot) {
            core::state::macro::macroAutomationClearSlot(pages_->automation, address);
        }
        return false;
    }

    persistConfigChange(stateRefs_(), operations_);
    return true;
}

}  // namespace core::handler
