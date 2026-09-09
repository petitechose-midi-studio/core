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

    // Only bindings/scales and one automation curve change. Prepare the graph
    // first; curve replacement checks every failure before its first write.
    auto& domain = pages.control.authored();
    auto pending = core::app::makeExtmemUniqueCopy(domain.modulation);
    if (!pending || !applyModulationAssignmentsToGraph(*pending, target.modulation) ||
        !validProjectModulationDomain(*pending, domain.curves, &domain.automation)) return false;
    if (!replaceProjectControlAutomationInDomain(
            domain,
            address,
            target.automation.automation,
            target.automation.pointCount > 0U
                ? target.automation.points.get() : nullptr,
            target.automation.pointCount
        )) {
        return false;
    }
    domain.modulation = *pending;
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

FLASHMEM void syncPageStructureTrack(
    MacroPagesState& pages,
    uint8_t track
) {
    if (pages.currentActiveTrack() != track) return;
    pages.syncActiveTrackCache();
    pages.updateActiveConfigs();
}

FLASHMEM bool applyPageStructureHistory(
    MacroPagesState& pages,
    const MacroPageStructureHistoryPayload& payload,
    bool after
) {
    if (payload.track >= TRACK_COUNT ||
        !sameMacroTrackData(pages.tracks[payload.track],
                           after ? payload.beforeTrack : payload.afterTrack) ||
        !payload.control.matches(pages.control.authored(), !after)) {
        return false;
    }
    if (payload.control.changed()) {
        payload.control.apply(pages.control);
        pages.control.markAuthoredMutation();
    }
    pages.tracks[payload.track] = after ? payload.afterTrack : payload.beforeTrack;
    syncPageStructureTrack(pages, payload.track);
    return true;
}

}  // namespace history_detail

}  // namespace core::state::macro
