#include "state/macro/MacroHistory.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::macro {

namespace {

FLASHMEM bool sameCurveMetadata(
    const MacroAutomationCurveRef& lhs,
    const MacroAutomationCurveRef& rhs
) {
    return lhs.active == rhs.active &&
           lhs.playbackState == rhs.playbackState &&
           lhs.pointCount == rhs.pointCount &&
           lhs.sourceDurationTicks == rhs.sourceDurationTicks &&
           lhs.durationTicks == rhs.durationTicks &&
           lhs.windowOffsetTicks == rhs.windowOffsetTicks &&
           lhs.interpolation == rhs.interpolation &&
           lhs.modulationOrigin == rhs.modulationOrigin;
}

FLASHMEM bool sameFloatBits(float lhs, float rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
}

FLASHMEM bool samePoint(
    const MacroPackedCurvePoint& lhs,
    const MacroPackedCurvePoint& rhs
) {
    return lhs.tick == rhs.tick && lhs.value == rhs.value;
}

FLASHMEM uint16_t snapshotPointCount(const MacroSlotHistorySnapshot& snapshot) {
    return static_cast<uint16_t>(
        snapshot.automationPointCount + snapshot.modulationPointCount
    );
}

FLASHMEM bool snapshotConsistent(const MacroSlotHistorySnapshot& snapshot) {
    if (!macroAutomationAddressValid(snapshot.address)) return false;
    const uint32_t total = static_cast<uint32_t>(snapshot.automationPointCount) +
                           snapshot.modulationPointCount;
    if (total > MACRO_HISTORY_POINT_CAPACITY) return false;
    if (!snapshot.slotPresent) {
        return snapshot.automationPointCount == 0 &&
               snapshot.modulationPointCount == 0;
    }
    if (snapshot.slot.automation.active != (snapshot.automationPointCount > 0) ||
        snapshot.slot.modulation.active != (snapshot.modulationPointCount > 0) ||
        snapshot.slot.automation.pointCount != snapshot.automationPointCount ||
        snapshot.slot.modulation.pointCount != snapshot.modulationPointCount) {
        return false;
    }
    if (snapshot.slot.automation.active && snapshot.slot.automation.pointOffset != 0) {
        return false;
    }
    return !snapshot.slot.modulation.active ||
           snapshot.slot.modulation.pointOffset == snapshot.automationPointCount;
}

FLASHMEM void normalizeCurveOffsets(MacroSlotHistorySnapshot& snapshot) {
    snapshot.slot.automation.pointOffset = 0;
    snapshot.slot.modulation.pointOffset = snapshot.automationPointCount;
}

FLASHMEM bool liveProjectCurveMatches(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ProjectCurveId curveId,
    const MacroAutomationCurveRef& live,
    const MacroAutomationCurveRef& expected,
    const MacroSlotHistorySnapshot& snapshot,
    uint16_t snapshotOffset
) {
    if (!sameCurveMetadata(live, expected)) return false;
    if (!live.active) return !core::state::modulation::valid(curveId);
    const auto* record = core::state::modulation::findProjectCurve(
        control.authored.curves,
        curveId
    );
    if (record == nullptr || record->pointCount != live.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            control.authored.curves.pointCount ||
        static_cast<uint32_t>(snapshotOffset) + live.pointCount >
            snapshot.points.size()) {
        return false;
    }
    for (uint16_t i = 0; i < live.pointCount; ++i) {
        const auto& point = control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + i)
        ];
        const MacroPackedCurvePoint livePoint{point.tick, point.value};
        if (!samePoint(
                livePoint,
                snapshot.points[static_cast<uint16_t>(snapshotOffset + i)]
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sameAddress(
    const MacroAutomationSlotAddress& lhs,
    const MacroAutomationSlotAddress& rhs
) {
    return macroAutomationAddressEquals(lhs, rhs);
}

}  // namespace

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

    if (!core::state::modulation::captureProjectControlMacroSlot(
            pages.control,
            address,
            out.slot,
            out.points.data(),
            static_cast<uint16_t>(out.points.size()),
            out.automationPointCount,
            out.modulationPointCount
        )) {
        return false;
    }
    out.slotPresent = macroCurveStored(out.slot.automation) ||
                      macroCurveStored(out.slot.modulation);
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
        lhs.modulationPointCount != rhs.modulationPointCount) {
        return false;
    }
    if (!lhs.slotPresent) return true;
    if (!sameCurveMetadata(lhs.slot.automation, rhs.slot.automation) ||
        !sameCurveMetadata(lhs.slot.modulation, rhs.slot.modulation) ||
        !sameFloatBits(lhs.slot.modulationDepth, rhs.slot.modulationDepth)) {
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

    core::state::modulation::ProjectControlMacroSlotView live{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            live
        ) || live.present != snapshot.slotPresent) {
        return false;
    }
    if (!live.present) return true;
    return sameFloatBits(
               live.legacy.modulationDepth,
               snapshot.slot.modulationDepth
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.automationCurveId,
               live.legacy.automation,
               snapshot.slot.automation,
               snapshot,
               0
           ) &&
           liveProjectCurveMatches(
               pages.control,
               live.modulationCurveId,
               live.legacy.modulation,
               snapshot.slot.modulation,
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
    MacroAutomationSlotState empty{};
    const auto& slot = snapshot.slotPresent ? snapshot.slot : empty;
    const auto* points = required > 0U ? snapshot.points.data() : nullptr;
    if (!core::state::modulation::replaceProjectControlMacroSlot(
            pages.control,
            address,
            slot,
            points,
            required
        )) {
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

FLASHMEM MacroHistoryService::MacroHistoryService() = default;
FLASHMEM MacroHistoryService::~MacroHistoryService() = default;

FLASHMEM MacroHistoryChangePtr MacroHistoryService::prepare(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroHistoryActionKind kind
) const {
    auto change = core::app::makeExtmemUnique<MacroHistoryChange>();
    if (!change) return {};
    change->kind = kind;
    change->address = address;
    if (!captureMacroSlotHistorySnapshot(pages, address, change->before)) {
        return {};
    }
    return change;
}

FLASHMEM bool MacroHistoryService::commitPrepared(
    MacroPagesState& pages,
    MacroHistoryChangePtr change,
    bool coalesce
) {
    if (!change || !sameAddress(change->address, change->before.address)) {
        return false;
    }
    if (!captureMacroSlotHistorySnapshot(pages, change->address, change->after)) {
        (void)applyMacroSlotHistorySnapshot(pages, change->before);
        return false;
    }
    if (sameMacroSlotHistorySnapshot(change->before, change->after)) {
        return false;
    }

    if (coalesce && coalescing_ && undo_count_ > 0 &&
        coalesced_kind_ == change->kind &&
        sameAddress(coalesced_address_, change->address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous &&
            sameMacroSlotHistorySnapshot(previous->after, change->before)) {
            previous->after = change->after;
            clearRedo_();
            return true;
        }
    }

    push_(undo_, undo_count_, std::move(change));
    clearRedo_();
    coalescing_ = coalesce;
    if (coalescing_) {
        coalesced_kind_ = undo_[undo_count_ - 1U]->kind;
        coalesced_address_ = undo_[undo_count_ - 1U]->address;
    }
    return true;
}

FLASHMEM bool MacroHistoryService::setModulationDepthCoalesced(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    float depth
) {
    if (!macroAutomationAddressValid(address) || !std::isfinite(depth)) return false;
    core::state::modulation::ProjectControlMacroSlotView slot{};
    if (!core::state::modulation::readProjectControlMacroSlot(
            pages.control,
            address,
            slot
        ) || !slot.modulationStored || slot.legacyMutationAmbiguous) {
        return false;
    }
    const float next = std::clamp(depth, 0.0f, 1.0f);
    if (sameFloatBits(next, slot.legacy.modulationDepth)) return false;

    if (coalescing_ && undo_count_ > 0 &&
        coalesced_kind_ == MacroHistoryActionKind::DEPTH_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && liveMacroSlotMatchesHistorySnapshot(pages, previous->after)) {
            if (!core::state::modulation::setProjectControlModulationAmount(
                    pages.control,
                    address,
                    next
                )) {
                return false;
            }
            core::state::modulation::ProjectControlMacroSlotView updated{};
            if (!core::state::modulation::readProjectControlMacroSlot(
                    pages.control,
                    address,
                    updated
                )) {
                return false;
            }
            previous->after.slot.modulationDepth =
                updated.legacy.modulationDepth;
            clearRedo_();
            return true;
        }
    }

    auto change = prepare(pages, address, MacroHistoryActionKind::DEPTH_EDIT);
    if (!change) return false;
    if (!core::state::modulation::setProjectControlModulationAmount(
            pages.control,
            address,
            next
        )) {
        return false;
    }
    return commitPrepared(pages, std::move(change), true);
}

FLASHMEM void MacroHistoryService::endCoalescing() {
    coalescing_ = false;
}

FLASHMEM bool MacroHistoryService::undo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress
) {
    endCoalescing();
    if (undo_count_ == 0) return false;
    auto& change = undo_[undo_count_ - 1U];
    if (!change || !liveMacroSlotMatchesHistorySnapshot(pages, change->after) ||
        !applyMacroSlotHistorySnapshot(pages, change->before)) {
        return false;
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --undo_count_;
    push_(redo_, redo_count_, std::move(applied));
    return true;
}

FLASHMEM bool MacroHistoryService::redo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress
) {
    endCoalescing();
    if (redo_count_ == 0) return false;
    auto& change = redo_[redo_count_ - 1U];
    if (!change || !liveMacroSlotMatchesHistorySnapshot(pages, change->before) ||
        !applyMacroSlotHistorySnapshot(pages, change->after)) {
        return false;
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --redo_count_;
    push_(undo_, undo_count_, std::move(applied));
    return true;
}

FLASHMEM void MacroHistoryService::clear() {
    for (auto& entry : undo_) entry.reset();
    for (auto& entry : redo_) entry.reset();
    undo_count_ = 0;
    redo_count_ = 0;
    endCoalescing();
}

FLASHMEM void MacroHistoryService::push_(
    std::array<MacroHistoryChangePtr, ENTRY_LIMIT>& stack,
    uint8_t& count,
    MacroHistoryChangePtr change
) {
    if (!change) return;
    if (count >= ENTRY_LIMIT) {
        for (uint8_t i = 1; i < ENTRY_LIMIT; ++i) {
            stack[i - 1U] = std::move(stack[i]);
        }
        stack[ENTRY_LIMIT - 1U].reset();
        count = static_cast<uint8_t>(ENTRY_LIMIT - 1U);
    }
    stack[count++] = std::move(change);
}

FLASHMEM void MacroHistoryService::clearRedo_() {
    for (auto& entry : redo_) entry.reset();
    redo_count_ = 0;
}

}  // namespace core::state::macro
