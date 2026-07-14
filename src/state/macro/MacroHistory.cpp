#include "state/macro/MacroHistory.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

namespace {

FLASHMEM bool curveRangeValid(
    const MacroAutomationCurveRef& curve,
    const MacroAutomationPointPool& pool
) {
    if (!curve.active) return curve.pointCount == 0;
    if (curve.pointCount == 0 || curve.pointOffset >= pool.used) return false;
    const uint32_t end = static_cast<uint32_t>(curve.pointOffset) + curve.pointCount;
    return end <= pool.used && end <= MACRO_AUTOMATION_POINT_POOL_CAPACITY;
}

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

FLASHMEM bool appendLiveCurvePoints(
    const MacroAutomationCurveRef& curve,
    const MacroAutomationPointPool& pool,
    MacroSlotHistorySnapshot& out,
    uint16_t& cursor
) {
    if (!curveRangeValid(curve, pool)) return !curve.active;
    if (!curve.active) return true;
    if (static_cast<uint32_t>(cursor) + curve.pointCount > out.points.size()) {
        return false;
    }
    for (uint16_t i = 0; i < curve.pointCount; ++i) {
        out.points[static_cast<uint16_t>(cursor + i)] =
            pool.points[static_cast<uint16_t>(curve.pointOffset + i)];
    }
    cursor = static_cast<uint16_t>(cursor + curve.pointCount);
    return true;
}

FLASHMEM bool liveCurveMatches(
    const MacroAutomationCurveRef& live,
    const MacroAutomationPointPool& pool,
    const MacroAutomationCurveRef& expected,
    const MacroSlotHistorySnapshot& snapshot,
    uint16_t snapshotOffset
) {
    if (!sameCurveMetadata(live, expected)) return false;
    if (!live.active) return true;
    if (!curveRangeValid(live, pool) ||
        static_cast<uint32_t>(snapshotOffset) + live.pointCount > snapshot.points.size()) {
        return false;
    }
    for (uint16_t i = 0; i < live.pointCount; ++i) {
        if (!samePoint(
                pool.points[static_cast<uint16_t>(live.pointOffset + i)],
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

    const auto* slot = macroAutomationFindSlot(pages.automation, address);
    if (slot == nullptr) return true;
    if (!curveRangeValid(slot->automation, pages.automation.pointPool) ||
        !curveRangeValid(slot->modulation, pages.automation.pointPool)) {
        return false;
    }
    const uint32_t total = static_cast<uint32_t>(slot->automation.pointCount) +
                           slot->modulation.pointCount;
    if (total > MACRO_HISTORY_POINT_CAPACITY) return false;

    out.slotPresent = true;
    out.slot = *slot;
    out.automationPointCount = slot->automation.active
        ? slot->automation.pointCount
        : 0;
    out.modulationPointCount = slot->modulation.active
        ? slot->modulation.pointCount
        : 0;
    uint16_t cursor = 0;
    if (!appendLiveCurvePoints(
            slot->automation,
            pages.automation.pointPool,
            out,
            cursor
        ) ||
        !appendLiveCurvePoints(
            slot->modulation,
            pages.automation.pointPool,
            out,
            cursor
        )) {
        return false;
    }
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

    const auto* live = macroAutomationFindSlot(pages.automation, address);
    if ((live != nullptr) != snapshot.slotPresent) return false;
    if (live == nullptr) return true;
    return sameFloatBits(live->modulationDepth, snapshot.slot.modulationDepth) &&
           liveCurveMatches(
               live->automation,
               pages.automation.pointPool,
               snapshot.slot.automation,
               snapshot,
               0
           ) &&
           liveCurveMatches(
               live->modulation,
               pages.automation.pointPool,
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
    auto* current = macroAutomationFindMutableSlot(pages.automation, address);
    const uint16_t reclaimable = current != nullptr
        ? macroAutomationStoredPointCount(*current, pages.automation.pointPool)
        : 0;
    const uint16_t free = static_cast<uint16_t>(
        MACRO_AUTOMATION_POINT_POOL_CAPACITY - pages.automation.pointPool.used
    );
    const uint16_t required = snapshotPointCount(snapshot);
    if (static_cast<uint32_t>(required) >
        static_cast<uint32_t>(free) + reclaimable) {
        return false;
    }
    if (snapshot.slotPresent && current == nullptr &&
        pages.automation.entryCount >= MACRO_AUTOMATION_SLOT_CAPACITY) {
        return false;
    }

    if (!snapshot.slotPresent) {
        if (current != nullptr) {
            (void)macroAutomationClearSlot(pages.automation, address);
        }
    } else {
        if (current != nullptr) {
            current->automation = {};
            current->modulation = {};
            current->modulationDepth = 0.0f;
            macroAutomationCompactPool(pages.automation);
        } else {
            current = macroAutomationGetOrCreateSlot(pages.automation, address);
            if (current == nullptr) return false;
        }

        const uint16_t start = pages.automation.pointPool.used;
        for (uint16_t i = 0; i < required; ++i) {
            pages.automation.pointPool.points[static_cast<uint16_t>(start + i)] =
                snapshot.points[i];
        }
        pages.automation.pointPool.used = static_cast<uint16_t>(start + required);
        *current = snapshot.slot;
        if (current->automation.active) {
            current->automation.pointOffset = start;
        }
        if (current->modulation.active) {
            current->modulation.pointOffset = static_cast<uint16_t>(
                start + snapshot.automationPointCount
            );
        }
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
    auto* slot = macroAutomationFindMutableSlot(pages.automation, address);
    if (slot == nullptr || !macroCurveStored(slot->modulation)) return false;
    const float next = std::clamp(depth, 0.0f, 1.0f);
    if (sameFloatBits(next, slot->modulationDepth)) return false;

    if (coalescing_ && undo_count_ > 0 &&
        coalesced_kind_ == MacroHistoryActionKind::DEPTH_EDIT &&
        sameAddress(coalesced_address_, address)) {
        auto& previous = undo_[undo_count_ - 1U];
        if (previous && liveMacroSlotMatchesHistorySnapshot(pages, previous->after)) {
            const float previousDepth = slot->modulationDepth;
            MacroSlotHistorySnapshot nextAfter{};
            slot->modulationDepth = next;
            if (!captureMacroSlotHistorySnapshot(pages, address, nextAfter)) {
                // Capture clears its output before validating. Never capture
                // directly into the committed endpoint or a failed coalesced
                // turn would destroy the only exact rollback snapshot.
                slot->modulationDepth = previousDepth;
                return false;
            }
            previous->after = nextAfter;
            clearRedo_();
            return true;
        }
    }

    auto change = prepare(pages, address, MacroHistoryActionKind::DEPTH_EDIT);
    if (!change) return false;
    slot->modulationDepth = next;
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
