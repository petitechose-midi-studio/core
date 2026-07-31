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

namespace history_detail {

FLASHMEM bool captureMacroSlotDeletionState(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotDeletionState& out
) {
    if (!macroAutomationAddressValid(address) ||
        !captureMacroAutomationHistorySnapshot(
            pages,
            address,
            out.automation
        ) || !captureModulationAssignments(
            pages,
            address,
            out.modulation
        )) {
        return false;
    }
    const auto& page = pages.pageData(address.track, address.page);
    out.macroActive = page.isMacroActive(address.macro);
    out.cc = page.cc[address.macro];
    out.staticValue = page.values[address.macro];
    return true;
}

FLASHMEM bool liveMacroSlotDeletionStateMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroSlotDeletionState& expected
) {
    if (!macroAutomationAddressValid(address) ||
        !macroAutomationAddressEquals(expected.automation.address, address) ||
        expected.modulation.destination !=
            core::state::modulation::projectControlDestination(address)) {
        return false;
    }
    const auto& page = pages.pageData(address.track, address.page);
    return page.isMacroActive(address.macro) == expected.macroActive &&
           page.cc[address.macro] == expected.cc &&
           sameFloatBits(page.values[address.macro], expected.staticValue) &&
           liveMacroAutomationMatchesHistorySnapshot(
               pages,
               expected.automation
           ) && liveModulationAssignmentsMatch(pages, expected.modulation);
}

FLASHMEM bool applyMacroSlotDeletionState(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroSlotDeletionState& target
) {
    using namespace core::state::modulation;
    if (!macroAutomationAddressValid(address) ||
        !macroAutomationAddressEquals(target.automation.address, address) ||
        target.modulation.destination != projectControlDestination(address)) {
        return false;
    }

    // Removal and its history replay are cold structural operations. Build the
    // complete Project Control result in one PSRAM scratch object so failure can
    // never expose a half-cleared destination to the realtime runtime.
    auto pending = core::app::makeExtmemUnique<ProjectControlDomainState>();
    if (!pending) return false;
    *pending = pages.control.authored;
    if (!replaceProjectControlAutomationInDomain(
            *pending,
            address,
            target.automation.automation,
            target.automation.pointCount > 0U
                ? target.automation.points.get() : nullptr,
            target.automation.pointCount
        ) || !applyModulationAssignmentsToGraph(
            pending->modulation,
            target.modulation
        ) || !validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }

    pages.control.authored = *pending;
    pages.control.markAuthoredMutation();
    auto& page = pages.pageData(address.track, address.page);
    page.setMacroActive(address.macro, target.macroActive);
    page.cc[address.macro] = target.cc;
    page.values[address.macro] = target.staticValue;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM uint64_t pageStructureControlHash(
    const core::state::modulation::ProjectControlDomainState& domain
) {
    return hashBytes64(
        14695981039346656037ULL,
        &domain,
        sizeof(domain)
    );
}

FLASHMEM void syncPageStructureTrack(
    MacroPagesState& pages,
    uint8_t track
) {
    if (pages.currentActiveTrack() != track) return;
    pages.syncActiveTrackCache();
    pages.updateActiveConfigs();
}

FLASHMEM bool pageStructureBeforeMatches(
    const MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload
) {
    return payload.beforeControl != nullptr &&
        payload.track < TRACK_COUNT &&
        sameMacroTrackData(
            pages.tracks[payload.track],
            payload.beforeTrack
        ) &&
        std::memcmp(
            &pages.control.authored,
            payload.beforeControl.get(),
            sizeof(core::state::modulation::ProjectControlDomainState)
        ) == 0;
}

FLASHMEM bool pageStructureAfterMatches(
    const MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload
) {
    return payload.track < TRACK_COUNT &&
        sameMacroTrackData(
            pages.tracks[payload.track],
            payload.afterTrack
        ) &&
        pageStructureControlHash(pages.control.authored) ==
            payload.afterControlHash;
}

FLASHMEM bool applyPageStructureHistory(
    MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload,
    bool after
) {
    if (payload.beforeControl == nullptr || payload.track >= TRACK_COUNT ||
        (payload.operation == MacroPageStructureHistoryOperation::COMPACT &&
         payload.retainedPageMask == 0U)) {
        return false;
    }
    if (!after) {
        if (!pageStructureAfterMatches(pages, payload)) return false;
        pages.control.authored = *payload.beforeControl;
        pages.control.markAuthoredMutation();
        pages.tracks[payload.track] = payload.beforeTrack;
        syncPageStructureTrack(pages, payload.track);
        return pageStructureBeforeMatches(pages, payload);
    }

    if (!pageStructureBeforeMatches(pages, payload)) return false;
    if (payload.operation == MacroPageStructureHistoryOperation::SNAPSHOT) {
        if (payload.afterControl != nullptr) {
            pages.control.authored = *payload.afterControl;
            pages.control.markAuthoredMutation();
        } else if (pageStructureControlHash(pages.control.authored) !=
                   payload.afterControlHash) {
            return false;
        }
        pages.tracks[payload.track] = payload.afterTrack;
    } else {
        if (!core::state::modulation::compactProjectControlPages(
                pages.control,
                payload.track,
                payload.retainedPageMask
            )) {
            return false;
        }
        if (!pages.tracks[payload.track].compactPages(
                payload.retainedPageMask
            )) {
            pages.control.authored = *payload.beforeControl;
            pages.control.markAuthoredMutation();
            return false;
        }
    }
    syncPageStructureTrack(pages, payload.track);
    if (pageStructureAfterMatches(pages, payload)) return true;

    pages.control.authored = *payload.beforeControl;
    pages.control.markAuthoredMutation();
    pages.tracks[payload.track] = payload.beforeTrack;
    syncPageStructureTrack(pages, payload.track);
    return false;
}

}  // namespace history_detail

}  // namespace core::state::macro
