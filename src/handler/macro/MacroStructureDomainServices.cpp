#include "handler/macro/MacroStructureDomainServices.hpp"

#include "state/shared/StructureSlotOps.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

namespace structure_slots = core::state::shared;

namespace {

void syncActivePagePresentation(core::state::CoreState& state) {
    state.statusBar.pageName.set(state.pages.activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
}

void finalizeStructureChange(core::state::CoreState& state) {
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    syncActivePagePresentation(state);
    state.requestMacroWorkspacePersist();
}

void applyPageStructureMutation(core::state::CoreState& state,
                                uint16_t enabledMask,
                                uint8_t activePage) {
    state.flushAutoPersist();
    state.pages.activeTrackData().enabledPageMask = enabledMask;
    state.pages.syncActiveTrackCache();
    state.pages.setActivePage(activePage);
    finalizeStructureChange(state);
}

void applyTrackStructureMutation(core::state::CoreState& state,
                                 uint16_t enabledMask,
                                 uint8_t activeTrack) {
    state.flushAutoPersist();
    state.setSharedTrackState(enabledMask, activeTrack);
    finalizeStructureChange(state);
}

void applyTrackStructureState(core::state::CoreState& state,
                              uint16_t enabledMask,
                              uint8_t activeTrack) {
    state.setSharedTrackState(enabledMask, activeTrack);
    finalizeStructureChange(state);
}

void persistConfigChange(core::state::CoreState& state) {
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
    state.requestMacroWorkspacePersist();
}

}  // namespace

MacroStructureDomainServices::MacroStructureDomainServices(core::state::CoreState& state)
    : state_(&state) {}

MacroStructureDomainServices MacroStructureDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return MacroStructureDomainServices{state};
}

void MacroStructureDomainServices::switchToPage(uint8_t pageIndex) const {
    core::state::macro::MacroWorkflow::switchToPage(*state_, pageIndex);
}

void MacroStructureDomainServices::switchToTrack(uint8_t trackIndex) const {
    core::state::macro::MacroWorkflow::switchToTrack(*state_, trackIndex);
}

uint8_t MacroStructureDomainServices::activeTrack() const {
    return state_->currentSharedActiveTrack();
}

uint16_t MacroStructureDomainServices::pageEnabledMask() const {
    return state_->pages.currentEnabledPageMask();
}

uint16_t MacroStructureDomainServices::trackEnabledMask() const {
    return state_->currentSharedTrackEnabledMask();
}

bool MacroStructureDomainServices::deleteActivePage() const {
    const auto mutation = structure_slots::removeIndex(
        state_->pages.currentEnabledPageMask(),
        state_->pages.currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    applyPageStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::deleteActiveTrack() const {
    const auto mutation = structure_slots::removeIndex(
        state_->currentSharedTrackEnabledMask(),
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    applyTrackStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::deleteSelectedPages(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        state_->pages.currentEnabledPageMask(),
        selectedMask,
        state_->pages.currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    applyPageStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::deleteSelectedTracks(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        state_->currentSharedTrackEnabledMask(),
        selectedMask,
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    applyTrackStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    return true;
}

bool MacroStructureDomainServices::duplicateSelectedPages(uint16_t selectedMask) const {
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        state_->pages.currentEnabledPageMask(),
        selectedMask,
        core::state::macro::PAGE_COUNT,
        [this](uint8_t source, uint8_t dest) {
            state_->pages.activeTrackData().pages[dest] = state_->pages.activeTrackData().pages[source];
        }
    );
    if (!result.changed) return false;

    applyPageStructureMutation(
        *state_,
        result.nextMask,
        result.firstDuplicated < core::state::macro::PAGE_COUNT
            ? result.firstDuplicated
            : state_->pages.currentActivePage()
    );
    return true;
}

bool MacroStructureDomainServices::duplicateSelectedTracks(uint16_t selectedMask) const {
    const auto result = structure_slots::duplicateSelectionIntoFreeSlots(
        state_->currentSharedTrackEnabledMask(),
        selectedMask,
        core::state::macro::TRACK_COUNT,
        [this](uint8_t source, uint8_t dest) {
            state_->pages.tracks[dest] = state_->pages.tracks[source];
        }
    );
    if (!result.changed) return false;

    applyTrackStructureMutation(
        *state_,
        result.nextMask,
        result.firstDuplicated < core::state::macro::TRACK_COUNT
            ? result.firstDuplicated
            : activeTrack()
    );
    return true;
}

bool MacroStructureDomainServices::erasePage(uint8_t pageIndex) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;
    if (!state_->pages.activeTrackData().isPageEnabled(pageIndex)) return false;

    state_->flushAutoPersist();
    state_->pages.activeTrackData().pages[pageIndex].initDefault(pageIndex);
    if (state_->pages.currentActivePage() == pageIndex) {
        state_->pages.setActivePage(pageIndex);
        syncActivePagePresentation(*state_);
    }
    persistConfigChange(*state_);
    return true;
}

bool MacroStructureDomainServices::eraseTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if (!state_->pages.isTrackEnabled(trackIndex)) return false;

    state_->flushAutoPersist();
    state_->pages.tracks[trackIndex].initDefaults(trackIndex);
    if (activeTrack() == trackIndex) {
        state_->setSharedTrackState(trackEnabledMask(), trackIndex);
        syncActivePagePresentation(*state_);
    }
    persistConfigChange(*state_);
    return true;
}

bool MacroStructureDomainServices::pastePage(
    uint8_t pageIndex,
    const core::state::macro::MacroPageData& pageData
) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;

    state_->flushAutoPersist();
    state_->pages.activeTrackData().pages[pageIndex] = pageData;
    state_->pages.activeTrackData().setPageEnabled(pageIndex, true);
    state_->pages.syncActiveTrackCache();
    state_->pages.setActivePage(pageIndex);
    finalizeStructureChange(*state_);
    return true;
}

bool MacroStructureDomainServices::pasteTrack(
    uint8_t trackIndex,
    const core::state::macro::MacroTrackData& trackData
) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;

    state_->flushAutoPersist();
    state_->pages.tracks[trackIndex] = trackData;
    applyTrackStructureState(
        *state_,
        static_cast<uint16_t>(
            state_->currentSharedTrackEnabledMask() |
            structure_slots::slotBit(trackIndex)
        ),
        trackIndex
    );
    return true;
}

bool MacroStructureDomainServices::createNextPage() const {
    const uint16_t enabledMask = state_->pages.currentEnabledPageMask();
    const int nextPage = structure_slots::nextAddIndexAfterHighest(
        enabledMask,
        core::state::macro::PAGE_COUNT
    );
    if (nextPage < 0) return false;

    const uint8_t index = static_cast<uint8_t>(nextPage);
    state_->pages.activeTrackData().pages[index].initDefault(index);
    applyPageStructureMutation(
        *state_,
        static_cast<uint16_t>(enabledMask | structure_slots::slotBit(index)),
        index
    );
    return true;
}

bool MacroStructureDomainServices::createTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if ((state_->currentSharedTrackEnabledMask() & structure_slots::slotBit(trackIndex)) != 0) {
        return false;
    }

    state_->pages.tracks[trackIndex].initDefaults(trackIndex);
    applyTrackStructureMutation(
        *state_,
        static_cast<uint16_t>(
            state_->currentSharedTrackEnabledMask() | structure_slots::slotBit(trackIndex)
        ),
        trackIndex
    );
    return true;
}

}  // namespace core::handler
