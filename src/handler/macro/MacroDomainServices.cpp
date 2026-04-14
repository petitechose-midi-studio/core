#include "handler/macro/MacroDomainServices.hpp"

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

MacroDomainServices::MacroDomainServices(core::state::CoreState& state)
    : state_(&state) {}

MacroDomainServices MacroDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroDomainServices{state};
}

void MacroDomainServices::syncPreviewState_() const {
    state_->macroUi.syncPreviewTrack(activeTrack());
    state_->macroUi.syncPreviewPage(state_->pages.currentActivePage());
}

float MacroDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(state_->macros, index);
}

void MacroDomainServices::setRuntimeValue(uint8_t index, float value) const {
    state_->noteMacroInteraction();
    core::state::macro::MacroWorkflow::setRuntimeValue(state_->macros, index, value);
}

const core::state::macro::MacroConfig& MacroDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(state_->pages, index);
}

bool MacroDomainServices::setConfig(uint8_t index, uint8_t channel, uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfig(*state_, index, channel, cc);
}

bool MacroDomainServices::setTrackConfigs(
    const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
) const {
    const uint8_t targetChannel = configs[0].channel;
    for (uint8_t i = 1; i < core::state::macro::MACRO_COUNT; ++i) {
        if (configs[i].channel != targetChannel) {
            return false;
        }
    }

    const bool channelChanged = state_->pages.activeTrackChannel() != targetChannel;
    bool anyCcChanged = false;

    auto& page = state_->pages.activePageData();
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        if (page.cc[i] == configs[i].cc) continue;
        page.cc[i] = configs[i].cc;
        anyCcChanged = true;
    }

    if (!channelChanged && !anyCcChanged) {
        return false;
    }

    if (channelChanged) {
        state_->pages.setActiveTrackChannel(targetChannel);
    } else {
        state_->pages.updateActiveConfigs();
    }

    state_->configRevision.set(
        core::state::macro::nextMacroConfigRevision(state_->configRevision.get())
    );
    state_->requestMacroWorkspacePersist();
    return true;
}

bool MacroDomainServices::setConfigCc(uint8_t index, uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfigCc(*state_, index, cc);
}

bool MacroDomainServices::setTrackChannel(uint8_t channel) const {
    return core::state::macro::MacroWorkflow::setTrackChannel(*state_, channel);
}

void MacroDomainServices::switchToPage(uint8_t pageIndex) const {
    core::state::macro::MacroWorkflow::switchToPage(*state_, pageIndex);
    syncPreviewState_();
}

void MacroDomainServices::switchToTrack(uint8_t trackIndex) const {
    core::state::macro::MacroWorkflow::switchToTrack(*state_, trackIndex);
    syncPreviewState_();
}

uint8_t MacroDomainServices::activeTrack() const {
    return state_->currentSharedActiveTrack();
}

uint8_t MacroDomainServices::activeTrackChannel() const {
    return state_->pages.activeTrackChannel();
}

bool MacroDomainServices::isActivePageEnabled() const {
    return state_->pages.isPageEnabled(state_->pages.currentActivePage());
}

void MacroDomainServices::togglePageEnabled(uint8_t pageIndex) const {
    state_->pages.togglePageEnabled(pageIndex);
}

bool MacroDomainServices::deleteActivePage() const {
    const auto mutation = structure_slots::removeIndex(
        state_->pages.currentEnabledPageMask(),
        state_->pages.currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    applyPageStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::deleteActiveTrack() const {
    const auto mutation = structure_slots::removeIndex(
        state_->currentSharedTrackEnabledMask(),
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    applyTrackStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::deleteSelectedPages(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        state_->pages.currentEnabledPageMask(),
        selectedMask,
        state_->pages.currentActivePage(),
        core::state::macro::PAGE_COUNT
    );
    if (!mutation.changed) return false;

    applyPageStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::deleteSelectedTracks(uint16_t selectedMask) const {
    const auto mutation = structure_slots::removeSelected(
        state_->currentSharedTrackEnabledMask(),
        selectedMask,
        activeTrack(),
        core::state::macro::TRACK_COUNT
    );
    if (!mutation.changed) return false;

    applyTrackStructureMutation(*state_, mutation.nextMask, mutation.nextActive);
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::duplicateSelectedPages(uint16_t selectedMask) const {
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
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::duplicateSelectedTracks(uint16_t selectedMask) const {
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
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::erasePage(uint8_t pageIndex) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;
    if (!state_->pages.activeTrackData().isPageEnabled(pageIndex)) return false;

    state_->flushAutoPersist();
    state_->pages.activeTrackData().pages[pageIndex].initDefault(pageIndex);
    if (state_->pages.currentActivePage() == pageIndex) {
        state_->pages.setActivePage(pageIndex);
        syncActivePagePresentation(*state_);
        syncPreviewState_();
    }
    persistConfigChange(*state_);
    return true;
}

bool MacroDomainServices::eraseTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if (!state_->pages.isTrackEnabled(trackIndex)) return false;

    state_->flushAutoPersist();
    state_->pages.tracks[trackIndex].initDefaults(trackIndex);
    if (activeTrack() == trackIndex) {
        state_->setSharedTrackState(trackEnabledMask(), trackIndex);
        syncActivePagePresentation(*state_);
        syncPreviewState_();
    }
    persistConfigChange(*state_);
    return true;
}

bool MacroDomainServices::pastePage(
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
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::pasteTrack(
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
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::createNextPage() const {
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
    syncPreviewState_();
    return true;
}

bool MacroDomainServices::createNextTrack() const {
    const uint16_t enabledMask = state_->currentSharedTrackEnabledMask();
    const int nextTrack = structure_slots::nextAddIndexAfterHighest(
        enabledMask,
        core::state::macro::TRACK_COUNT
    );
    if (nextTrack < 0) return false;

    const uint8_t index = static_cast<uint8_t>(nextTrack);
    state_->pages.tracks[index].initDefaults(index);
    applyTrackStructureMutation(
        *state_,
        static_cast<uint16_t>(enabledMask | structure_slots::slotBit(index)),
        index
    );
    syncPreviewState_();
    return true;
}

uint16_t MacroDomainServices::pageEnabledMask() const {
    return state_->pages.currentEnabledPageMask();
}

uint16_t MacroDomainServices::trackEnabledMask() const {
    return state_->currentSharedTrackEnabledMask();
}

void MacroDomainServices::pulseCcIn() const {
    state_->statusBar.pulseCcIn();
}

void MacroDomainServices::pulseCcOut() const {
    state_->statusBar.pulseCcOut();
}

void MacroDomainServices::pulseNoteIn() const {
    state_->statusBar.pulseNoteIn();
}

}  // namespace core::handler
