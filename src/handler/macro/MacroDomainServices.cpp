#include "handler/macro/MacroDomainServices.hpp"

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

namespace {

uint8_t countEnabledPages(uint16_t enabledMask) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < core::state::macro::PAGE_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            ++count;
        }
    }
    return count;
}

uint8_t countEnabledTracks(uint16_t enabledMask) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < core::state::macro::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            ++count;
        }
    }
    return count;
}

uint8_t countBits(uint16_t mask, uint8_t count) {
    uint8_t bits = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if ((mask & static_cast<uint16_t>(1U << i)) != 0) {
            ++bits;
        }
    }
    return bits;
}

uint8_t nextEnabledIndex(uint16_t enabledMask, uint8_t current, uint8_t count) {
    for (uint8_t offset = 1; offset < count; ++offset) {
        const uint8_t candidate = static_cast<uint8_t>((current + offset) % count);
        if ((enabledMask & static_cast<uint16_t>(1U << candidate)) != 0) {
            return candidate;
        }
    }
    return current;
}

int nextAvailableIndexAfterHighest(uint16_t enabledMask, uint8_t count) {
    for (int index = static_cast<int>(count) - 1; index >= 0; --index) {
        if ((enabledMask & static_cast<uint16_t>(1U << static_cast<uint8_t>(index))) == 0) {
            continue;
        }
        const int next = index + 1;
        return (next < count) ? next : -1;
    }
    return (count > 0) ? 0 : -1;
}

}  // namespace

MacroDomainServices::MacroDomainServices(StateRefs state, Hooks hooks)
    : macros_(&state.macros)
    , pages_(&state.pages)
    , config_revision_(&state.configRevision)
    , status_bar_(&state.statusBar)
    , hooks_(hooks) {}

MacroDomainServices MacroDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroDomainServices{
        StateRefs{
            state.macros,
            state.pages,
            state.configRevision,
            state.statusBar,
        },
        Hooks{&state},
    };
}

float MacroDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(*macros_, index);
}

void MacroDomainServices::setRuntimeValue(uint8_t index, float value) const {
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, value);
}

const core::state::macro::MacroConfig& MacroDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(*pages_, index);
}

bool MacroDomainServices::setConfig(uint8_t index, uint8_t channel, uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfig(
        core::state::macro::MacroWorkflow::StateRefs{
            *macros_,
            *pages_,
            *config_revision_,
            *status_bar_,
        },
        hooks_,
        index,
        channel,
        cc
    );
}

bool MacroDomainServices::setConfigCc(uint8_t index, uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfigCc(
        core::state::macro::MacroWorkflow::StateRefs{
            *macros_,
            *pages_,
            *config_revision_,
            *status_bar_,
        },
        hooks_,
        index,
        cc
    );
}

bool MacroDomainServices::setTrackChannel(uint8_t channel) const {
    return core::state::macro::MacroWorkflow::setTrackChannel(
        core::state::macro::MacroWorkflow::StateRefs{
            *macros_,
            *pages_,
            *config_revision_,
            *status_bar_,
        },
        hooks_,
        channel
    );
}

void MacroDomainServices::switchToPage(uint8_t pageIndex) const {
    core::state::macro::MacroWorkflow::switchToPage(
        core::state::macro::MacroWorkflow::StateRefs{
            *macros_,
            *pages_,
            *config_revision_,
            *status_bar_,
        },
        hooks_,
        pageIndex
    );
}

void MacroDomainServices::switchToTrack(uint8_t trackIndex) const {
    core::state::macro::MacroWorkflow::switchToTrack(
        core::state::macro::MacroWorkflow::StateRefs{
            *macros_,
            *pages_,
            *config_revision_,
            *status_bar_,
        },
        hooks_,
        trackIndex
    );
}

uint8_t MacroDomainServices::activeTrack() const {
    return pages_->activeTrack;
}

uint8_t MacroDomainServices::activeTrackChannel() const {
    return pages_->activeTrackChannel();
}

bool MacroDomainServices::isActivePageEnabled() const {
    return pages_->isPageEnabled(pages_->activePage);
}

void MacroDomainServices::togglePageEnabled(uint8_t pageIndex) const {
    pages_->togglePageEnabled(pageIndex);
}

bool MacroDomainServices::deleteActivePage() const {
    const uint16_t enabledMask = pages_->enabledMask.get();
    const uint8_t currentPage = pages_->activePage;
    const uint16_t bit = static_cast<uint16_t>(1U << currentPage);
    if ((enabledMask & bit) == 0) return false;
    if (countEnabledPages(enabledMask) <= 1U) return false;

    hooks_.flushPendingRuntime();

    const uint16_t nextMask = enabledMask & static_cast<uint16_t>(~bit);
    const uint8_t nextPage =
        nextEnabledIndex(nextMask, currentPage, core::state::macro::PAGE_COUNT);

    pages_->activeTrackData().enabledPageMask = nextMask;
    pages_->syncActiveTrackCache();
    pages_->setActivePage(nextPage);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::deleteActiveTrack() const {
    const uint16_t enabledMask = pages_->trackEnabledMask.get();
    const uint8_t currentTrack = pages_->activeTrack;
    const uint16_t bit = static_cast<uint16_t>(1U << currentTrack);
    if ((enabledMask & bit) == 0) return false;
    if (countEnabledTracks(enabledMask) <= 1U) return false;

    hooks_.flushPendingRuntime();

    const uint16_t nextMask = enabledMask & static_cast<uint16_t>(~bit);
    const uint8_t nextTrack =
        nextEnabledIndex(nextMask, currentTrack, core::state::macro::TRACK_COUNT);

    pages_->trackEnabledMask.set(nextMask);
    pages_->setActiveTrack(nextTrack);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::deleteSelectedPages(uint16_t selectedMask) const {
    const uint16_t enabledMask = pages_->enabledMask.get();
    const uint16_t deleteMask = enabledMask & selectedMask;
    if (deleteMask == 0) return false;

    const uint8_t enabledCount = countEnabledPages(enabledMask);
    const uint8_t deleteCount = countBits(deleteMask, core::state::macro::PAGE_COUNT);
    if (deleteCount >= enabledCount) return false;

    hooks_.flushPendingRuntime();

    const uint16_t nextMask = enabledMask & static_cast<uint16_t>(~deleteMask);
    uint8_t nextPage = pages_->activePage;
    if ((nextMask & static_cast<uint16_t>(1U << nextPage)) == 0) {
        nextPage = nextEnabledIndex(nextMask, pages_->activePage, core::state::macro::PAGE_COUNT);
    }

    pages_->activeTrackData().enabledPageMask = nextMask;
    pages_->syncActiveTrackCache();
    pages_->setActivePage(nextPage);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::deleteSelectedTracks(uint16_t selectedMask) const {
    const uint16_t enabledMask = pages_->trackEnabledMask.get();
    const uint16_t deleteMask = enabledMask & selectedMask;
    if (deleteMask == 0) return false;

    const uint8_t enabledCount = countEnabledTracks(enabledMask);
    const uint8_t deleteCount = countBits(deleteMask, core::state::macro::TRACK_COUNT);
    if (deleteCount >= enabledCount) return false;

    hooks_.flushPendingRuntime();

    const uint16_t nextMask = enabledMask & static_cast<uint16_t>(~deleteMask);
    uint8_t nextTrack = pages_->activeTrack;
    if ((nextMask & static_cast<uint16_t>(1U << nextTrack)) == 0) {
        nextTrack = nextEnabledIndex(nextMask, pages_->activeTrack, core::state::macro::TRACK_COUNT);
    }

    pages_->trackEnabledMask.set(nextMask);
    pages_->setActiveTrack(nextTrack);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::duplicateSelectedPages(uint16_t selectedMask) const {
    const uint16_t enabledMask = pages_->enabledMask.get();
    const uint16_t sourceMask = enabledMask & selectedMask;
    if (sourceMask == 0) return false;

    uint16_t nextMask = enabledMask;
    uint8_t firstDuplicatedPage = core::state::macro::PAGE_COUNT;

    for (uint8_t source = 0; source < core::state::macro::PAGE_COUNT; ++source) {
        const uint16_t sourceBit = static_cast<uint16_t>(1U << source);
        if ((sourceMask & sourceBit) == 0) continue;

        uint8_t dest = core::state::macro::PAGE_COUNT;
        for (uint8_t candidate = 0; candidate < core::state::macro::PAGE_COUNT; ++candidate) {
            const uint16_t candidateBit = static_cast<uint16_t>(1U << candidate);
            if ((nextMask & candidateBit) == 0) {
                dest = candidate;
                break;
            }
        }
        if (dest >= core::state::macro::PAGE_COUNT) break;

        pages_->activeTrackData().pages[dest] = pages_->activeTrackData().pages[source];
        nextMask |= static_cast<uint16_t>(1U << dest);
        if (firstDuplicatedPage >= core::state::macro::PAGE_COUNT) {
            firstDuplicatedPage = dest;
        }
    }

    if (nextMask == enabledMask) return false;

    hooks_.flushPendingRuntime();
    pages_->activeTrackData().enabledPageMask = nextMask;
    pages_->syncActiveTrackCache();
    if (firstDuplicatedPage < core::state::macro::PAGE_COUNT) {
        pages_->setActivePage(firstDuplicatedPage);
    }
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::duplicateSelectedTracks(uint16_t selectedMask) const {
    const uint16_t enabledMask = pages_->trackEnabledMask.get();
    const uint16_t sourceMask = enabledMask & selectedMask;
    if (sourceMask == 0) return false;

    uint16_t nextMask = enabledMask;
    uint8_t firstDuplicatedTrack = core::state::macro::TRACK_COUNT;

    for (uint8_t source = 0; source < core::state::macro::TRACK_COUNT; ++source) {
        const uint16_t sourceBit = static_cast<uint16_t>(1U << source);
        if ((sourceMask & sourceBit) == 0) continue;

        uint8_t dest = core::state::macro::TRACK_COUNT;
        for (uint8_t candidate = 0; candidate < core::state::macro::TRACK_COUNT; ++candidate) {
            const uint16_t candidateBit = static_cast<uint16_t>(1U << candidate);
            if ((nextMask & candidateBit) == 0) {
                dest = candidate;
                break;
            }
        }
        if (dest >= core::state::macro::TRACK_COUNT) break;

        pages_->tracks[dest] = pages_->tracks[source];
        nextMask |= static_cast<uint16_t>(1U << dest);
        if (firstDuplicatedTrack >= core::state::macro::TRACK_COUNT) {
            firstDuplicatedTrack = dest;
        }
    }

    if (nextMask == enabledMask) return false;

    hooks_.flushPendingRuntime();
    pages_->trackEnabledMask.set(nextMask);
    if (firstDuplicatedTrack < core::state::macro::TRACK_COUNT) {
        pages_->setActiveTrack(firstDuplicatedTrack);
    }
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::erasePage(uint8_t pageIndex) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;
    if (!pages_->activeTrackData().isPageEnabled(pageIndex)) return false;

    hooks_.flushPendingRuntime();
    pages_->activeTrackData().pages[pageIndex].initDefault(pageIndex);
    if (pages_->activePage == pageIndex) {
        pages_->setActivePage(pageIndex);
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
        status_bar_->pageName.set(pages_->activePageData().name);
    }
    config_revision_->set(config_revision_->get() + 1);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::eraseTrack(uint8_t trackIndex) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;
    if (!pages_->isTrackEnabled(trackIndex)) return false;

    hooks_.flushPendingRuntime();
    pages_->tracks[trackIndex].initDefaults(trackIndex);
    if (pages_->activeTrack == trackIndex) {
        pages_->setActiveTrack(trackIndex);
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
        status_bar_->pageName.set(pages_->activePageData().name);
    }
    config_revision_->set(config_revision_->get() + 1);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::pastePage(
    uint8_t pageIndex,
    const core::state::macro::MacroPageData& pageData
) const {
    if (pageIndex >= core::state::macro::PAGE_COUNT) return false;

    hooks_.flushPendingRuntime();
    pages_->activeTrackData().pages[pageIndex] = pageData;
    pages_->activeTrackData().setPageEnabled(pageIndex, true);
    pages_->syncActiveTrackCache();
    pages_->setActivePage(pageIndex);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::pasteTrack(
    uint8_t trackIndex,
    const core::state::macro::MacroTrackData& trackData
) const {
    if (trackIndex >= core::state::macro::TRACK_COUNT) return false;

    hooks_.flushPendingRuntime();
    pages_->tracks[trackIndex] = trackData;
    pages_->setTrackEnabled(trackIndex, true);
    pages_->setActiveTrack(trackIndex);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::createNextPage() const {
    const uint16_t enabledMask = pages_->enabledMask.get();
    const int nextPage = nextAvailableIndexAfterHighest(enabledMask, core::state::macro::PAGE_COUNT);
    if (nextPage < 0) return false;

    hooks_.flushPendingRuntime();

    const uint8_t index = static_cast<uint8_t>(nextPage);
    pages_->activeTrackData().pages[index].initDefault(index);
    pages_->activeTrackData().enabledPageMask =
        enabledMask | static_cast<uint16_t>(1U << index);
    pages_->syncActiveTrackCache();
    pages_->setActivePage(index);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

bool MacroDomainServices::createNextTrack() const {
    const uint16_t enabledMask = pages_->trackEnabledMask.get();
    const int nextTrack = nextAvailableIndexAfterHighest(enabledMask, core::state::macro::TRACK_COUNT);
    if (nextTrack < 0) return false;

    hooks_.flushPendingRuntime();

    const uint8_t index = static_cast<uint8_t>(nextTrack);
    pages_->tracks[index].initDefaults(index);
    pages_->trackEnabledMask.set(enabledMask | static_cast<uint16_t>(1U << index));
    pages_->setActiveTrack(index);
    config_revision_->set(config_revision_->get() + 1);
    status_bar_->pageName.set(pages_->activePageData().name);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(*macros_, *pages_);
    hooks_.persistWorkspaceNow();
    return true;
}

void MacroDomainServices::setPageEnabledMask(uint16_t mask) const {
    pages_->activeTrackData().enabledPageMask = mask;
    pages_->syncActiveTrackCache();
}

uint16_t MacroDomainServices::pageEnabledMask() const {
    return pages_->enabledMask.get();
}

void MacroDomainServices::setTrackEnabledMask(uint16_t mask) const {
    pages_->trackEnabledMask.set(mask);
}

uint16_t MacroDomainServices::trackEnabledMask() const {
    return pages_->trackEnabledMask.get();
}

void MacroDomainServices::pulseCcIn() const {
    status_bar_->pulseCcIn();
}

void MacroDomainServices::pulseCcOut() const {
    status_bar_->pulseCcOut();
}

void MacroDomainServices::pulseNoteIn() const {
    status_bar_->pulseNoteIn();
}

}  // namespace core::handler
