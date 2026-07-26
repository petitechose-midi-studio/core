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
    uint8_t page
) const {
    if (pendingModulatorSlot_() != nullptr || track >= TRACK_COUNT ||
        page >= PAGE_COUNT ||
        !core::state::project::validProjectTrackSnapshot(
            projectTracks.authored
        )) {
        return {};
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
    payload.beforeTracks = projectTracks.authored;
    payload.beforeCc = pages.pageData(track, page).cc;
    payload.track = track;
    payload.page = page;
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
    const bool validAddress = payload.track < TRACK_COUNT &&
        payload.page < PAGE_COUNT &&
        change->address.track == payload.track &&
        change->address.page == payload.page;
    bool validCc = validAddress;
    if (validCc) {
        for (const uint8_t cc : pages.pageData(payload.track, payload.page).cc) {
            if (cc > 127U) {
                validCc = false;
                break;
            }
        }
    }
    if (!validCc ||
        !core::state::project::validProjectTrackSnapshot(
            projectTracks.authored
        )) {
        if (validAddress) {
            pages.pageData(payload.track, payload.page).cc = payload.beforeCc;
        }
        if (!core::state::project::sameProjectTrackSnapshot(
                projectTracks.authored,
                payload.beforeTracks
            )) {
            (void)core::state::project::applyProjectTrackSnapshot(
                projectTracks,
                payload.beforeTracks
            );
        }
        pages.updateActiveConfigs();
        return false;
    }
    payload.afterCc = pages.pageData(payload.track, payload.page).cc;
    payload.afterTracks = projectTracks.authored;
    const bool sameCc = payload.beforeCc == payload.afterCc;
    const bool sameTracks = core::state::project::sameProjectTrackSnapshot(
        payload.beforeTracks,
        payload.afterTracks
    );
    if (sameCc && sameTracks) return false;
    endCoalescing();
    recordNewEntry_(std::move(change));
    return true;
}

}  // namespace core::state::macro
