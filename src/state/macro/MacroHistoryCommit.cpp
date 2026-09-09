#include "state/macro/MacroHistoryInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
namespace core::state::macro {

using namespace history_detail;

FLASHMEM bool MacroHistoryService::commitPrepared(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (change && change->automation) {
        if (coalesce ||
            change->kind != MacroHistoryActionKind::RECORD_AUTOMATION ||
            !sameAddress(
                change->address,
                change->automation->before.address
            )) {
            return false;
        }
        auto& payload = *change->automation;
        if (!captureMacroAutomationHistorySnapshot(
                pages,
                change->address,
                payload.after
            )) {
            (void)applyMacroAutomationHistorySnapshot(pages, payload.before);
            return false;
        }
        if (sameMacroAutomationHistorySnapshot(
                payload.before,
                payload.after
            )) {
            return false;
        }
        recordNewEntry_(std::move(change));
        endCoalescing();
        return true;
    }
    if (!change || !change->slot ||
        !sameAddress(change->address, change->slot->before.address)) {
        return false;
    }
    if (!captureMacroSlotHistorySnapshot(
            pages,
            change->address,
            change->slot->after
        )) {
        (void)applyMacroSlotHistorySnapshot(pages, change->slot->before);
        return false;
    }
    if (sameMacroSlotHistorySnapshot(
            change->slot->before,
            change->slot->after
        )) {
        return false;
    }

    if (coalesce && coalescing_ && undo_count_ > 0 &&
        coalesced_kind_ == change->kind &&
        sameAddress(coalesced_address_, change->address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && previous->slot &&
            sameMacroSlotHistorySnapshot(
                previous->slot->after,
                change->slot->before
            )) {
            previous->slot->after = change->slot->after;
            clearRedo_();
            return true;
        }
    }

    recordNewEntry_(std::move(change));
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = undo_[undo_count_ - 1U]->kind;
        coalesced_address_ = undo_[undo_count_ - 1U]->address;
    }
    return true;
}

FLASHMEM MacroHistoryChangePtr MacroHistoryService::prepareTrackConfig(
    const MacroPagesState& pages,
    const core::state::project::ProjectTrackState& projectTracks,
    uint8_t track,
    uint8_t page,
    uint8_t ccMask,
    bool includeChannel
) const {
    if (pendingModulatorSlot_() != nullptr || track >= TRACK_COUNT ||
        page >= PAGE_COUNT || ccMask == 0U) {
        return {};
    }
    const uint8_t midiChannel =
        core::state::project::projectTrackMidiChannel(projectTracks, track);
    if (!core::state::project::validProjectTrackMidiChannel(midiChannel)) {
        return {};
    }
    const auto& cc = pages.pageData(track, page).cc;
    for (uint8_t i = 0U; i < MACRO_COUNT; ++i) {
        if ((ccMask & (1U << i)) != 0U && cc[i] > 127U) return {};
    }
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->auxiliary = core::app::makeExtmemUnique<
        MacroAuxiliaryHistoryPayload
    >();
    if (!change->auxiliary) return {};
    change->kind = MacroHistoryActionKind::CONFIG_EDIT;
    change->address = {
        .track = track,
        .page = page,
        .macro = 0U,
    };
    auto& payload = change->auxiliary->trackConfig;
    payload.beforeCc = pages.pageData(track, page).cc;
    payload.beforeMidiChannel = midiChannel;
    payload.track = track;
    payload.page = page;
    payload.ccMask = ccMask;
    payload.includeChannel = includeChannel;
    payload.valid = true;
    return change;
}

FLASHMEM bool MacroHistoryService::commitPreparedTrackConfig(
    MacroPagesState& pages,
    core::state::project::ProjectTrackState& projectTracks,
    MacroHistoryChangePtr change
) {
    if (!change || !change->auxiliary ||
        !change->auxiliary->trackConfig.valid) {
        return false;
    }
    auto& payload = change->auxiliary->trackConfig;
    const bool validAddress = payload.ccMask != 0U && payload.track < TRACK_COUNT &&
        payload.page < PAGE_COUNT &&
        change->address.track == payload.track &&
        change->address.page == payload.page;
    bool validCc = validAddress;
    if (validCc) {
        const auto& cc = pages.pageData(payload.track, payload.page).cc;
        for (uint8_t i = 0U; i < MACRO_COUNT; ++i) {
            if ((payload.ccMask & (1U << i)) != 0U && cc[i] > 127U) {
                validCc = false;
                break;
            }
        }
    }
    const uint8_t midiChannel = validAddress
        ? core::state::project::projectTrackMidiChannel(
              projectTracks,
              payload.track
          )
        : 0U;
    const bool validRouting = validAddress &&
        core::state::project::validProjectTrackMidiChannel(midiChannel) &&
        core::state::project::validProjectTrackMidiChannel(
            payload.beforeMidiChannel
        );
    if (!validCc || !validRouting) {
        if (validAddress) {
            payload.applyCc(pages.pageData(payload.track, payload.page).cc, payload.beforeCc);
            if (payload.includeChannel && midiChannel != payload.beforeMidiChannel) {
                (void)core::state::project::setProjectTrackMidiChannel(
                    projectTracks,
                    payload.track,
                    payload.beforeMidiChannel
                );
            }
        }
        pages.updateActiveConfigs();
        return false;
    }
    payload.afterCc = pages.pageData(payload.track, payload.page).cc;
    payload.afterMidiChannel = midiChannel;
    const bool sameCc = payload.matchesCc(payload.beforeCc, payload.afterCc);
    const bool sameRouting =
        !payload.includeChannel || payload.beforeMidiChannel == payload.afterMidiChannel;
    if (sameCc && sameRouting) return false;
    endCoalescing();
    recordNewEntry_(std::move(change));
    return true;
}

}  // namespace core::state::macro
