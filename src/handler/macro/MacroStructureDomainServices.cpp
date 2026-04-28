#include "handler/macro/MacroStructureDomainServices.hpp"

#include "state/shared/StructureSlotOps.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

using StateRefs = MacroStructureDomainServices::StateRefs;
using Operations = MacroStructureDomainServices::Operations;

void flushAutoPersist(Operations operations) {
    if (operations.flushAutoPersist != nullptr) {
        operations.flushAutoPersist(operations.context);
    }
}

void requestPersist(Operations operations) {
    if (operations.requestPersist != nullptr) {
        operations.requestPersist(operations.context);
    }
}

bool setSharedTrackState(Operations operations, uint16_t enabledMask, uint8_t activeTrack) {
    return operations.setSharedTrackState != nullptr &&
           operations.setSharedTrackState(operations.context, enabledMask, activeTrack);
}

void syncActivePagePresentation(StateRefs state) {
    state.statusBar.pageName.set(state.pages.activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
}

void finalizeStructureChange(StateRefs state, Operations operations) {
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    syncActivePagePresentation(state);
    requestPersist(operations);
}

void applyPageStructureMutation(StateRefs state,
                                Operations operations,
                                uint16_t enabledMask,
                                uint8_t activePage) {
    flushAutoPersist(operations);
    state.pages.activeTrackData().enabledPageMask = enabledMask;
    state.pages.syncActiveTrackCache();
    state.pages.setActivePage(activePage);
    finalizeStructureChange(state, operations);
}

void applyTrackStructureMutation(StateRefs state,
                                 Operations operations,
                                 uint16_t enabledMask,
                                 uint8_t activeTrack) {
    flushAutoPersist(operations);
    setSharedTrackState(operations, enabledMask, activeTrack);
    finalizeStructureChange(state, operations);
}

void applyTrackStructureState(StateRefs state,
                              Operations operations,
                              uint16_t enabledMask,
                              uint8_t activeTrack) {
    setSharedTrackState(operations, enabledMask, activeTrack);
    finalizeStructureChange(state, operations);
}

void persistConfigChange(StateRefs state, Operations operations) {
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    requestPersist(operations);
}

void flushAutoPersistFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->flushAutoPersist();
}

void requestPersistFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->requestMacroWorkspacePersist();
}

bool setSharedTrackStateFromCoreState(void* context, uint16_t enabledMask, uint8_t activeTrack) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr && state->setSharedTrackState(enabledMask, activeTrack);
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

}  // namespace

MacroStructureDomainServices::MacroStructureDomainServices(
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

MacroStructureDomainServices MacroStructureDomainServices::fromCoreState(
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
            requestPersistFromCoreState,
            setSharedTrackStateFromCoreState,
            switchToPageFromCoreState,
            switchToTrackFromCoreState,
        },
    };
}

MacroStructureDomainServices::StateRefs MacroStructureDomainServices::stateRefs_() const {
    return StateRefs{
        *macros_,
        *pages_,
        *config_revision_,
        *status_bar_,
        *shared_track_active_,
        *shared_track_enabled_mask_,
    };
}

void MacroStructureDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
    }
}

void MacroStructureDomainServices::switchToTrack(uint8_t trackIndex) const {
    if (operations_.switchToTrack != nullptr) {
        operations_.switchToTrack(operations_.context, trackIndex);
    }
}

uint8_t MacroStructureDomainServices::activeTrack() const {
    return shared_track_active_->get();
}

uint16_t MacroStructureDomainServices::pageEnabledMask() const {
    return pages_->currentEnabledPageMask();
}

uint16_t MacroStructureDomainServices::trackEnabledMask() const {
    return shared_track_enabled_mask_->get();
}

bool MacroStructureDomainServices::deleteActivePage() const {
    const auto mutation = structure_slots::removeIndex(
        pages_->currentEnabledPageMask(),
        pages_->currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    applyPageStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::deleteActiveTrack() const {
    const auto mutation = structure_slots::removeIndex(
        shared_track_enabled_mask_->get(),
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    applyTrackStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::deleteSelectedPages(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        pages_->currentEnabledPageMask(),
        selectedMask,
        pages_->currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    applyPageStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::deleteSelectedTracks(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        shared_track_enabled_mask_->get(),
        selectedMask,
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    applyTrackStructureMutation(stateRefs_(), operations_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::duplicateSelectedPages(uint16_t selectedMask) const {
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        pages_->currentEnabledPageMask(),
        selectedMask,
        core::state::macro::PAGE_COUNT,
        [this](uint8_t source, uint8_t dest) {
            pages_->activeTrackData().pages[dest] = pages_->activeTrackData().pages[source];
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

bool MacroStructureDomainServices::duplicateSelectedTracks(uint16_t selectedMask) const {
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        shared_track_enabled_mask_->get(),
        selectedMask,
        core::state::macro::TRACK_COUNT,
        [this](uint8_t source, uint8_t dest) {
            pages_->tracks[dest] = pages_->tracks[source];
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

bool MacroStructureDomainServices::erasePage(uint8_t pageIndex) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;
    if (!pages_->activeTrackData().isPageEnabled(pageIndex)) return false;

    flushAutoPersist(operations_);
    pages_->activeTrackData().pages[pageIndex].initDefault(pageIndex);
    if (pages_->currentActivePage() == pageIndex) {
        pages_->setActivePage(pageIndex);
        syncActivePagePresentation(stateRefs_());
    }
    persistConfigChange(stateRefs_(), operations_);
    return true;
}

bool MacroStructureDomainServices::eraseTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if (!pages_->isTrackEnabled(trackIndex)) return false;

    flushAutoPersist(operations_);
    pages_->tracks[trackIndex].initDefaults(trackIndex);
    if (activeTrack() == trackIndex) {
        setSharedTrackState(operations_, trackEnabledMask(), trackIndex);
        syncActivePagePresentation(stateRefs_());
    }
    persistConfigChange(stateRefs_(), operations_);
    return true;
}

bool MacroStructureDomainServices::pastePage(
    uint8_t pageIndex,
    const core::state::macro::MacroPageData& pageData
) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;

    flushAutoPersist(operations_);
    pages_->activeTrackData().pages[pageIndex] = pageData;
    pages_->activeTrackData().setPageEnabled(pageIndex, true);
    pages_->syncActiveTrackCache();
    pages_->setActivePage(pageIndex);
    finalizeStructureChange(stateRefs_(), operations_);
    return true;
}

bool MacroStructureDomainServices::pasteTrack(
    uint8_t trackIndex,
    const core::state::macro::MacroTrackData& trackData
) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;

    flushAutoPersist(operations_);
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

bool MacroStructureDomainServices::createNextPage() const {
    const uint16_t enabledMask = pages_->currentEnabledPageMask();
    const int nextPage = structure_slots::nextAddIndexAfterHighest(
        enabledMask,
        core::state::macro::PAGE_COUNT
    );
    if (nextPage < 0) return false;

    const uint8_t index = static_cast<uint8_t>(nextPage);
    pages_->activeTrackData().pages[index].initDefault(index);
    applyPageStructureMutation(
        stateRefs_(),
        operations_,
        static_cast<uint16_t>(enabledMask | structure_slots::slotBit(index)),
        index
    );
    return true;
}

bool MacroStructureDomainServices::createTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if ((shared_track_enabled_mask_->get() & structure_slots::slotBit(trackIndex)) != 0) {
        return false;
    }

    pages_->tracks[trackIndex].initDefaults(trackIndex);
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

}  // namespace core::handler
