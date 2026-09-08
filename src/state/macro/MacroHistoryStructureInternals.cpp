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

FLASHMEM void xorPageStructureControl(
    uint8_t* target,
    const uint8_t* source
) {
    constexpr size_t bytes =
        sizeof(core::state::modulation::ProjectControlDomainState);
    static_assert(bytes % sizeof(uint32_t) == 0U);
    for (size_t index = 0U;
         index < bytes;
         index += sizeof(uint32_t)) {
        // memcpy keeps word-sized access valid without alignment/aliasing casts.
        uint32_t left, right;
        std::memcpy(&left, target + index, sizeof(left));
        std::memcpy(&right, source + index, sizeof(right));
        left ^= right;
        std::memcpy(target + index, &left, sizeof(left));
    }
}

FLASHMEM bool applyPageStructureHistory(
    MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload,
    bool after
) {
    if (payload.track >= TRACK_COUNT ||
        !sameMacroTrackData(pages.tracks[payload.track],
                           after ? payload.beforeTrack : payload.afterTrack) ||
        pageStructureControlHash(pages.control.authored) !=
            (after ? payload.beforeControlHash : payload.afterControlHash)) {
        return false;
    }
    if (payload.controlDelta) {
        // Unsigned-byte access is defined for this trivially copyable domain,
        // including arena tails. No allocation or structural reconstruction.
        xorPageStructureControl(
            reinterpret_cast<uint8_t*>(&pages.control.authored),
            payload.controlDelta.get()
        );
        pages.control.markAuthoredMutation();
    }
    pages.tracks[payload.track] = after ? payload.afterTrack : payload.beforeTrack;
    syncPageStructureTrack(pages, payload.track);
    return true;
}

}  // namespace history_detail

}  // namespace core::state::macro
