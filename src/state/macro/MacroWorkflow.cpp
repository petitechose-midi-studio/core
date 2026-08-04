#include "state/macro/MacroWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include <utility>

#include "state/CoreState.hpp"
#include "state/macro/MacroAutomationDomain.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::state::macro {

namespace {

FLASHMEM bool configsMatch(
    const std::array<MacroConfig, MACRO_COUNT>& lhs,
    const std::array<MacroConfig, MACRO_COUNT>& rhs
) {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (lhs[i].cc != rhs[i].cc) {
            return false;
        }
    }
    return true;
}

FLASHMEM int nextContiguousIndex(uint16_t mask, uint8_t count) {
    int highest = -1;
    for (uint8_t index = 0U; index < count; ++index) {
        if ((mask & static_cast<uint16_t>(1U << index)) != 0U) {
            highest = index;
        }
    }
    const int next = highest + 1;
    return next < count ? next : -1;
}

}  // namespace

FLASHMEM void MacroWorkflow::syncRuntimeFromActivePage(core::state::MacroState& macros,
                                                       const MacroPagesState& pages) {
    const auto& pageData = pages.activePageData();
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        char label[16];
        size_t pos = oc::type::text::appendString(label, sizeof(label), 0, "Macro ");
        pos = oc::type::text::appendUnsigned(label, sizeof(label), pos, i + 1);
        oc::type::text::terminate(label, sizeof(label), pos);
        macros.slots[i].label.set(label);
        setRuntimeValue(macros, i, pageData.values[i]);
    }
}

FLASHMEM void MacroWorkflow::syncActivePagePresentation(
    core::state::MacroState& macros,
    const MacroPagesState& pages,
    MacroUiState& macroUi
) noexcept {
    syncRuntimeFromActivePage(macros, pages);
    const uint8_t track = pages.currentActiveTrack();
    const uint8_t page = pages.currentActivePage();
    macroUi.refreshManualOverrideMask(track, page);
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        float manualValue = 0.0f;
        if (!macroUi.manualOverrides.valueFor(
                MacroAutomationSlotAddress{track, page, macro},
                manualValue
            )) {
            continue;
        }
        setRuntimeValue(macros, macro, manualValue);
    }
}

FLASHMEM void MacroWorkflow::switchToPage(CoreState& state, uint8_t pageIndex) {
    if (pageIndex >= PAGE_COUNT) return;

    const auto previousConfigs = state.pages.activeConfigs;
    state.flushProjectMutationCoalescing();
    state.pages.setActivePage(pageIndex);
    state.markProjectMutated();
    if (!configsMatch(previousConfigs, state.pages.activeConfigs)) {
        state.configRevision.set(nextMacroConfigRevision(state.configRevision.get()));
    }
    syncRuntimeFromActivePage(state.macros, state.pages);
}

FLASHMEM void MacroWorkflow::switchToTrack(CoreState& state, uint8_t trackIndex) {
    if (trackIndex >= TRACK_COUNT) return;

    const auto previousConfigs = state.pages.activeConfigs;
    state.flushProjectMutationCoalescing();
    state.setSharedTrackState(state.currentSharedTrackEnabledMask(), trackIndex);
    state.markProjectMutated();
    if (!configsMatch(previousConfigs, state.pages.activeConfigs)) {
        state.configRevision.set(nextMacroConfigRevision(state.configRevision.get()));
    }
    syncRuntimeFromActivePage(state.macros, state.pages);
}

FLASHMEM bool MacroWorkflow::setConfig(CoreState& state, uint8_t index, uint8_t channel, uint8_t cc) {
    if (index >= MACRO_COUNT) return false;
    if (channel > 15 || cc > 127) return false;

    auto& page = state.pages.activePageData();
    const uint8_t track = state.pages.currentActiveTrack();
    const bool channelChanged =
        project::projectTrackMidiChannel(state.projectTracks, track) != channel;
    const bool ccChanged = page.cc[index] != cc;
    if (!channelChanged && !ccChanged) {
        return false;
    }

    const MacroAutomationSlotAddress address{
        .track = track,
        .page = state.pages.currentActivePage(),
        .macro = index,
    };
    const auto previousProjectTracks = state.projectTracks.authored;
    auto configHistory = ccChanged
        ? state.macroHistory.prepare(
              state.pages,
              address,
              MacroHistoryActionKind::CONFIG_EDIT
          )
        : MacroHistoryChangePtr{};
    if (ccChanged && !configHistory) return false;
    if (channelChanged && ccChanged) {
        configHistory->auxiliary = core::app::makeExtmemUnique<
            MacroAuxiliaryHistoryPayload
        >();
        if (!configHistory->auxiliary) return false;
        configHistory->auxiliary->trackRouting.before = previousProjectTracks;
        configHistory->auxiliary->trackRouting.valid = true;
    }

    const uint8_t previousCc = page.cc[index];
    page.cc[index] = cc;
    if (channelChanged) {
        if (ccChanged) {
            if (!project::setProjectTrackMidiChannel(
                    state.projectTracks,
                    track,
                    channel
                ).changed()) {
                page.cc[index] = previousCc;
                state.pages.updateActiveConfigs();
                return false;
            }
            configHistory->auxiliary->trackRouting.after =
                state.projectTracks.authored;
            if (!state.macroHistory.commitPrepared(
                    state.pages,
                    std::move(configHistory)
                )) {
                page.cc[index] = previousCc;
                (void)project::applyProjectTrackSnapshot(
                    state.projectTracks,
                    previousProjectTracks
                );
                return false;
            }
            state.pages.updateActiveConfigs();
            state.configRevision.set(nextMacroConfigRevision(
                state.configRevision.get(),
                kMacroConfigDirtyAll
            ));
            state.markProjectMutated();
            return true;
        }
        auto trackDomain =
            project::ProjectTrackDomainServices::fromCoreState(state);
        if (!trackDomain.setMidiChannel(track, channel)) {
            page.cc[index] = previousCc;
            state.pages.updateActiveConfigs();
            return false;
        }
        if (ccChanged && !state.macroHistory.commitPrepared(
                state.pages,
                std::move(configHistory)
            )) {
            page.cc[index] = previousCc;
            (void)trackDomain.undo();
            state.projectTrackHistory.discardRedoBranch();
            return false;
        }
        // The canonical Track commit projects every runtime view, bumps
        // the all-config revision and marks the Project after the CC write.
        return true;
    }
    if (!state.macroHistory.commitPrepared(
            state.pages,
            std::move(configHistory)
        )) {
        page.cc[index] = previousCc;
        state.pages.updateActiveConfigs();
        return false;
    }
    // Config edits are content-only for Track identity. Refresh the active
    // projection only after history accepted the authored CC change.
    state.pages.updateActiveConfigs();
    state.configRevision.set(nextMacroConfigRevision(
        state.configRevision.get(),
        index
    ));
    state.markProjectMutated();
    return true;
}

FLASHMEM bool MacroWorkflow::setTrackChannel(CoreState& state, uint8_t channel) {
    if (channel > 15) return false;
    const uint8_t track = state.pages.currentActiveTrack();
    if (project::projectTrackMidiChannel(state.projectTracks, track) == channel) {
        return false;
    }
    return project::ProjectTrackDomainServices::fromCoreState(state)
        .setMidiChannel(track, channel);
}

FLASHMEM MacroSlotActivationPlan MacroWorkflow::planMacroSlotActivation(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    MacroSlotActivationPlan plan{};
    plan.address = address;
    if (!macroAutomationAddressValid(address)) return plan;

    const auto& page = pages.pageData(address.track, address.page);
    if (page.isMacroActive(address.macro)) {
        return plan;
    }
    plan.cc = defaultMacroCc(address.page, address.macro);
    plan.baseValue = 0.5f;
    plan.valid = true;
    return plan;
}

FLASHMEM bool MacroWorkflow::applyMacroSlotActivation(
    MacroPagesState& pages,
    const MacroSlotActivationPlan& plan
) {
    if (!plan.valid || !macroAutomationAddressValid(plan.address)) return false;
    const auto current = planMacroSlotActivation(pages, plan.address);
    if (!current.valid || current.cc != plan.cc ||
        current.baseValue != plan.baseValue) {
        return false;
    }

    auto& page = pages.pageData(plan.address.track, plan.address.page);
    page.cc[plan.address.macro] = plan.cc;
    page.values[plan.address.macro] = plan.baseValue;
    page.setMacroActive(plan.address.macro, true);
    if (pages.currentActiveTrack() == plan.address.track &&
        pages.currentActivePage() == plan.address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM MacroDestinationActivationPlan
MacroWorkflow::planDestinationActivation(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    MacroDestinationActivationPlan plan{};
    plan.address = address;
    if (!macroAutomationAddressValid(address)) return plan;

    plan.expectedTrackEnabledMask = pages.currentTrackEnabledMask();
    const bool trackEnabled = pages.isTrackEnabled(address.track);
    if (!trackEnabled) {
        const int nextTrack = nextContiguousIndex(
            plan.expectedTrackEnabledMask,
            TRACK_COUNT
        );
        if (nextTrack < 0 || address.track != static_cast<uint8_t>(nextTrack) ||
            address.page != 0U) {
            return plan;
        }
        plan.expectedPageEnabledMask =
            pages.tracks[address.track].enabledPageMask;
        plan.createTrack = true;
        plan.createPage = true;
        plan.createMacro = true;
        plan.valid = true;
        return plan;
    }

    const auto& track = pages.tracks[address.track];
    plan.expectedPageEnabledMask = track.enabledPageMask;
    if (!track.isPageEnabled(address.page)) {
        const int nextPage = nextContiguousIndex(
            track.enabledPageMask,
            PAGE_COUNT
        );
        if (nextPage < 0 || address.page != static_cast<uint8_t>(nextPage)) {
            return plan;
        }
        plan.createPage = true;
        plan.createMacro = true;
        plan.valid = true;
        return plan;
    }

    plan.createMacro = !track.pages[address.page].isMacroActive(address.macro);
    plan.valid = true;
    return plan;
}

FLASHMEM bool MacroWorkflow::applyDestinationActivation(
    MacroPagesState& pages,
    const MacroDestinationActivationPlan& plan
) {
    if (!plan.valid || !macroAutomationAddressValid(plan.address) ||
        pages.currentTrackEnabledMask() != plan.expectedTrackEnabledMask) {
        return false;
    }
    const auto live = planDestinationActivation(pages, plan.address);
    if (!live.valid || live.expectedTrackEnabledMask !=
            plan.expectedTrackEnabledMask ||
        live.expectedPageEnabledMask != plan.expectedPageEnabledMask ||
        live.createTrack != plan.createTrack ||
        live.createPage != plan.createPage ||
        live.createMacro != plan.createMacro) {
        return false;
    }

    const auto address = plan.address;
    if (plan.createTrack) {
        pages.tracks[address.track].initDefaults(address.track);
    }
    auto& track = pages.tracks[address.track];
    if (plan.createPage) {
        track.pages[address.page].initDefault(address.page);
        // A destination-created Page contains exactly the requested physical
        // Macro position; Macro 1 is not silently inserted as a prerequisite.
        track.pages[address.page].activeMacroMask = 0U;
        track.setPageEnabled(address.page, true);
    }
    if (plan.createMacro) {
        auto& page = track.pages[address.page];
        page.cc[address.macro] = defaultMacroCc(address.page, address.macro);
        page.values[address.macro] = 0.5f;
        page.setMacroActive(address.macro, true);
    }
    if (plan.createTrack) {
        pages.syncSharedTrackState(
            static_cast<uint16_t>(
                plan.expectedTrackEnabledMask |
                static_cast<uint16_t>(1U << address.track)
            ),
            pages.currentActiveTrack()
        );
    } else if (pages.currentActiveTrack() == address.track) {
        pages.syncActiveTrackCache();
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool MacroWorkflow::activateMacroSlot(core::state::MacroState& macros,
                                               MacroPagesState& pages,
                                               uint8_t index) {
    const MacroAutomationSlotAddress address{
        pages.currentActiveTrack(),
        pages.currentActivePage(),
        index,
    };
    const auto plan = planMacroSlotActivation(pages, address);
    if (!applyMacroSlotActivation(pages, plan)) return false;
    setRuntimeValue(macros, index, plan.baseValue);
    return true;
}

void MacroWorkflow::setRuntimeValue(core::state::MacroState& macros, uint8_t index, float value) {
    if (index >= MACRO_COUNT) return;
    macros.slots[index].value.set(macroAutomationClamp01(value));
}

float MacroWorkflow::runtimeValue(const core::state::MacroState& macros, uint8_t index) {
    if (index >= MACRO_COUNT) return 0.0f;
    return macros.slots[index].value.get();
}

const MacroConfig& MacroWorkflow::activeConfig(const MacroPagesState& pages, uint8_t index) {
    static const MacroConfig defaultConfig{};
    if (index >= MACRO_COUNT) return defaultConfig;
    return pages.activeConfigs[index];
}

}  // namespace core::state::macro
