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

FLASHMEM bool captureMacroSlotHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotHistorySnapshot& out
) {
    if (!macroAutomationAddressValid(address)) return false;
    out = {};
    out.address = address;
    const auto& page = pages.pageData(address.track, address.page);
    out.macroActive = page.isMacroActive(address.macro);
    out.cc = page.cc[address.macro];
    out.staticValue = page.values[address.macro];
    out.destinationScaleQ15 =
        core::state::modulation::projectModulationDestinationScaleQ15(
            pages.control.authored.modulation,
            core::state::modulation::projectControlDestination(address)
        );

    if (!core::state::modulation::captureProjectControlMacroDestination(
            pages.control,
            address,
            out.control,
            out.points.data(),
            static_cast<uint16_t>(out.points.size()),
            out.automationPointCount,
            out.modulationPointCount
        )) {
        return false;
    }
    out.slotPresent = out.control.present();
    normalizeCurveOffsets(out);
    return snapshotConsistent(out);
}

FLASHMEM bool sameMacroSlotHistorySnapshot(
    const MacroSlotHistorySnapshot& lhs,
    const MacroSlotHistorySnapshot& rhs
) {
    if (!sameAddress(lhs.address, rhs.address) ||
        lhs.macroActive != rhs.macroActive ||
        lhs.cc != rhs.cc ||
        !sameFloatBits(lhs.staticValue, rhs.staticValue) ||
        lhs.slotPresent != rhs.slotPresent ||
        lhs.automationPointCount != rhs.automationPointCount ||
        lhs.modulationPointCount != rhs.modulationPointCount ||
        lhs.destinationScaleQ15 != rhs.destinationScaleQ15) {
        return false;
    }
    if (!lhs.slotPresent) return true;
    if (!sameCurveMetadata(
            lhs.control.automation,
            rhs.control.automation
        ) ||
        !sameCurveMetadata(
            lhs.control.recordedShape,
            rhs.control.recordedShape
        ) ||
        !sameFloatBits(
            lhs.control.modulationAmount,
            rhs.control.modulationAmount
        )) {
        return false;
    }
    const uint16_t count = snapshotPointCount(lhs);
    for (uint16_t i = 0; i < count; ++i) {
        if (!samePoint(lhs.points[i], rhs.points[i])) return false;
    }
    return true;
}

FLASHMEM bool liveMacroSlotMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
) {
    if (!snapshotConsistent(snapshot)) return false;
    const auto& address = snapshot.address;
    const auto& page = pages.pageData(address.track, address.page);
    if (page.isMacroActive(address.macro) != snapshot.macroActive ||
        page.cc[address.macro] != snapshot.cc ||
        !sameFloatBits(page.values[address.macro], snapshot.staticValue)) {
        return false;
    }

    core::state::modulation::ProjectControlMacroDestinationView live{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            live
        ) || live.present() != snapshot.slotPresent ||
        core::state::modulation::projectModulationDestinationScaleQ15(
            pages.control.authored.modulation,
            core::state::modulation::projectControlDestination(address)
        ) != snapshot.destinationScaleQ15) {
        return false;
    }
    if (!live.present()) return true;
    return sameFloatBits(
               live.primaryModulation.amount,
               snapshot.control.modulationAmount
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.automation,
               snapshot.control.automation,
               snapshot,
               0
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.primaryModulation.recordedShape,
               snapshot.control.recordedShape,
               snapshot,
               snapshot.automationPointCount
           );
}

FLASHMEM bool applyMacroSlotHistorySnapshot(
    MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
) {
    if (!snapshotConsistent(snapshot)) return false;
    const auto& address = snapshot.address;
    const uint16_t required = snapshotPointCount(snapshot);
    core::state::modulation::ProjectControlMacroDestinationPayload empty{};
    const auto& control = snapshot.slotPresent ? snapshot.control : empty;
    const auto* points = required > 0U ? snapshot.points.data() : nullptr;
    if (!core::state::modulation::replaceProjectControlMacroDestination(
            pages.control,
            address,
            control,
            points,
            required
        )) {
        return false;
    }
    if (snapshot.destinationScaleQ15 !=
            core::state::modulation::
                PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 &&
        !core::state::modulation::setProjectModulationDestinationScale(
            pages.control.authored.modulation,
            core::state::modulation::projectControlDestination(address),
            snapshot.destinationScaleQ15
        ).changed()) {
        return false;
    }

    auto& page = pages.pageData(address.track, address.page);
    page.setMacroActive(address.macro, snapshot.macroActive);
    page.cc[address.macro] = snapshot.cc;
    page.values[address.macro] = snapshot.staticValue;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool captureMacroAutomationHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroAutomationHistorySnapshot& out
) {
    if (!macroAutomationAddressValid(address)) return false;
    out = MacroAutomationHistorySnapshot{};
    out.address = address;

    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            view
        )) {
        return false;
    }
    out.automation = {
        .spec = view.automation.spec,
        .pointOffset = 0U,
        .pointCount = view.automation.pointCount,
        .enabled = view.automation.enabled,
    };
    out.automation.pointOffset = 0U;
    if (!view.automation.stored()) {
        return automationSnapshotConsistent(out);
    }

    const auto* record = core::state::modulation::findProjectCurve(
        pages.control.authored.curves,
        view.automation.id
    );
    if (record == nullptr ||
        record->pointCount != view.automation.pointCount ||
        record->pointCount > MACRO_AUTOMATION_RECORDING_MAX_POINTS ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            pages.control.authored.curves.pointCount) {
        return false;
    }

    out.pointCount = record->pointCount;
    out.points = core::app::makeExtmemUniqueArrayForOverwrite<
        core::state::modulation::ProjectPackedCurvePoint
    >(out.pointCount);
    if (!out.points) return false;
    for (uint16_t index = 0; index < out.pointCount; ++index) {
        const auto& point = pages.control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + index)
        ];
        out.points[index] = point;
    }
    return automationSnapshotConsistent(out);
}

FLASHMEM bool sameMacroAutomationHistorySnapshot(
    const MacroAutomationHistorySnapshot& lhs,
    const MacroAutomationHistorySnapshot& rhs
) {
    if (!automationSnapshotConsistent(lhs) ||
        !automationSnapshotConsistent(rhs) ||
        !sameAddress(lhs.address, rhs.address) ||
        lhs.pointCount != rhs.pointCount ||
        !sameCurveMetadata(lhs.automation, rhs.automation)) {
        return false;
    }
    for (uint16_t index = 0; index < lhs.pointCount; ++index) {
        if (!samePoint(lhs.points[index], rhs.points[index])) return false;
    }
    return true;
}

FLASHMEM bool liveMacroAutomationMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
) {
    if (!automationSnapshotConsistent(snapshot)) return false;
    core::state::modulation::ProjectControlMacroDestinationView live{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            snapshot.address,
            live
        ) ||
        live.automation.stored() != (snapshot.pointCount > 0U) ||
        !sameCurveMetadata(live.automation, snapshot.automation)) {
        return false;
    }
    if (!live.automation.stored()) return true;

    const auto* record = core::state::modulation::findProjectCurve(
        pages.control.authored.curves,
        live.automation.id
    );
    if (record == nullptr || record->pointCount != snapshot.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            pages.control.authored.curves.pointCount) {
        return false;
    }
    for (uint16_t index = 0; index < snapshot.pointCount; ++index) {
        const auto& point = pages.control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + index)
        ];
        if (!samePoint(point, snapshot.points[index])) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool applyMacroAutomationHistorySnapshot(
    MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
) {
    if (!automationSnapshotConsistent(snapshot)) return false;
    return core::state::modulation::replaceProjectControlAutomation(
        pages.control,
        snapshot.address,
        snapshot.automation,
        snapshot.pointCount > 0U ? snapshot.points.get() : nullptr,
        snapshot.pointCount
    );
}

}  // namespace core::state::macro
