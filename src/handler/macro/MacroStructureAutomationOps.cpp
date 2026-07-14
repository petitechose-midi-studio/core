#include "handler/macro/MacroStructureAutomationOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::handler::macro_structure_automation_ops {

namespace {

namespace macro = core::state::macro;

enum class ScopeKind : uint8_t {
    PAGE,
    TRACK,
};

struct Scope {
    ScopeKind kind = ScopeKind::PAGE;
    uint8_t track = 0;
    uint8_t page = 0;
};

struct StorageUsage {
    uint16_t slots = 0;
    uint32_t points = 0;
};

FLASHMEM bool scopeValid(const Scope& scope) {
    return scope.track < macro::TRACK_COUNT &&
           (scope.kind == ScopeKind::TRACK || scope.page < macro::PAGE_COUNT);
}

FLASHMEM bool contains(const Scope& scope, const macro::MacroAutomationSlotAddress& address) {
    return address.track == scope.track &&
           (scope.kind == ScopeKind::TRACK || address.page == scope.page);
}

FLASHMEM StorageUsage bankUsage(const macro::MacroAutomationBankState& bank,
                                const Scope& scope,
                                bool contentOnly) {
    StorageUsage usage;
    const uint8_t count = std::min<uint8_t>(
        bank.entryCount,
        macro::MACRO_AUTOMATION_SLOT_CAPACITY
    );
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = bank.entries[i];
        if (!entry.active || !contains(scope, entry.address)) continue;
        if (contentOnly && !macro::macroAutomationSlotHasContent(entry.state)) continue;
        ++usage.slots;
        usage.points += macro::macroAutomationStoredPointCount(
            entry.state,
            bank.pointPool
        );
    }
    return usage;
}

FLASHMEM bool clipboardUsage(const core::state::MacroAutomationClipboard* clipboard,
                             bool trackScope,
                             StorageUsage& usage) {
    usage = {};
    if (clipboard == nullptr) return true;
    if (clipboard->trackScope != trackScope) return false;
    if (!clipboard->valid) return clipboard->count == 0;
    if (clipboard->count > clipboard->entries.size() ||
        clipboard->pointPool.used > clipboard->pointPool.points.size()) {
        return false;
    }

    const uint8_t count = clipboard->count;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = clipboard->entries[i];
        if (!entry.valid || entry.sourceMacro >= macro::MACRO_COUNT) return false;
        if (trackScope && entry.sourcePage >= macro::PAGE_COUNT) return false;
        if (!macro::macroAutomationSlotStateValidForMutation(
                entry.state,
                clipboard->pointPool
            )) {
            return false;
        }
        if (!macro::macroAutomationSlotHasContent(entry.state)) continue;

        for (uint8_t previous = 0; previous < i; ++previous) {
            const auto& candidate = clipboard->entries[previous];
            if (!candidate.valid || candidate.sourceMacro != entry.sourceMacro) continue;
            if (!trackScope || candidate.sourcePage == entry.sourcePage) return false;
        }

        ++usage.slots;
        usage.points += macro::macroAutomationStoredPointCount(
            entry.state,
            clipboard->pointPool
        );
    }
    return true;
}

FLASHMEM bool canReplace(const macro::MacroAutomationBankState& bank,
                         StorageUsage reclaimed,
                         StorageUsage incoming) {
    const uint32_t occupiedSlots = std::min<uint16_t>(
        bank.entryCount,
        macro::MACRO_AUTOMATION_SLOT_CAPACITY
    );
    if (reclaimed.slots > occupiedSlots || reclaimed.points > bank.pointPool.used) return false;

    const uint32_t resultingSlots = occupiedSlots - reclaimed.slots + incoming.slots;
    const uint32_t resultingPoints =
        static_cast<uint32_t>(bank.pointPool.used) - reclaimed.points + incoming.points;
    return resultingSlots <= macro::MACRO_AUTOMATION_SLOT_CAPACITY &&
           resultingPoints <= macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY;
}

FLASHMEM void clearScope(macro::MacroAutomationBankState& bank, const Scope& scope) {
    if (scope.kind == ScopeKind::TRACK) {
        macro::macroAutomationClearTrack(bank, scope.track);
    } else {
        macro::macroAutomationClearPage(bank, scope.track, scope.page);
    }
}

FLASHMEM bool copyBankSlot(macro::MacroAutomationBankState& bank,
                           const macro::MacroAutomationSlotAddress& sourceAddress,
                           const macro::MacroAutomationSlotAddress& destAddress) {
    const auto* source = macro::macroAutomationFindSlot(bank, sourceAddress);
    if (source == nullptr || !macro::macroAutomationSlotHasContent(*source)) return true;

    auto* dest = macro::macroAutomationGetOrCreateSlot(bank, destAddress);
    if (dest == nullptr) return false;
    if (macro::macroAutomationCopySlotState(bank, *dest, bank.pointPool, *source)) return true;

    macro::macroAutomationClearSlot(bank, destAddress);
    return false;
}

FLASHMEM bool copyClipboardSlot(
    macro::MacroAutomationBankState& bank,
    const macro::MacroAutomationSlotAddress& destAddress,
    const core::state::MacroAutomationClipboard& clipboard,
    const core::state::MacroAutomationClipboardEntry& source
) {
    if (!macro::macroAutomationSlotHasContent(source.state)) return true;
    auto* dest = macro::macroAutomationGetOrCreateSlot(bank, destAddress);
    if (dest == nullptr) return false;
    if (macro::macroAutomationCopySlotState(
            bank,
            *dest,
            clipboard.pointPool,
            source.state
        )) {
        return true;
    }

    macro::macroAutomationClearSlot(bank, destAddress);
    return false;
}

FLASHMEM bool duplicateScope(macro::MacroAutomationBankState& bank,
                             const Scope& source,
                             const Scope& dest) {
    if (!scopeValid(source) || !scopeValid(dest) || source.kind != dest.kind) return false;
    if (source.track == dest.track &&
        (source.kind == ScopeKind::TRACK || source.page == dest.page)) {
        return false;
    }

    const StorageUsage incoming = bankUsage(bank, source, true);
    const StorageUsage reclaimed = bankUsage(bank, dest, false);
    if (!canReplace(bank, reclaimed, incoming)) return false;

    clearScope(bank, dest);
    const uint8_t firstPage = source.kind == ScopeKind::TRACK ? 0 : source.page;
    const uint8_t pageCount = source.kind == ScopeKind::TRACK ? macro::PAGE_COUNT : 1;
    for (uint8_t pageOffset = 0; pageOffset < pageCount; ++pageOffset) {
        const uint8_t sourcePage = static_cast<uint8_t>(firstPage + pageOffset);
        const uint8_t destPage = source.kind == ScopeKind::TRACK ? sourcePage : dest.page;
        for (uint8_t macroIndex = 0; macroIndex < macro::MACRO_COUNT; ++macroIndex) {
            if (!copyBankSlot(
                    bank,
                    macro::MacroAutomationSlotAddress{
                        .track = source.track,
                        .page = sourcePage,
                        .macro = macroIndex,
                    },
                    macro::MacroAutomationSlotAddress{
                        .track = dest.track,
                        .page = destPage,
                        .macro = macroIndex,
                    }
                )) {
                clearScope(bank, dest);
                return false;
            }
        }
    }
    return true;
}

FLASHMEM bool replaceScopeFromClipboard(
    macro::MacroAutomationBankState& bank,
    const Scope& dest,
    const core::state::MacroAutomationClipboard* clipboard
) {
    if (!scopeValid(dest)) return false;

    StorageUsage incoming;
    const bool trackScope = dest.kind == ScopeKind::TRACK;
    if (!clipboardUsage(clipboard, trackScope, incoming)) return false;
    const StorageUsage reclaimed = bankUsage(bank, dest, false);
    if (!canReplace(bank, reclaimed, incoming)) return false;

    clearScope(bank, dest);
    if (clipboard == nullptr || !clipboard->valid) return true;

    const uint8_t count = clipboard->count;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = clipboard->entries[i];
        if (!entry.valid || !macro::macroAutomationSlotHasContent(entry.state)) continue;
        const uint8_t page = trackScope ? entry.sourcePage : dest.page;
        if (!copyClipboardSlot(
                bank,
                macro::MacroAutomationSlotAddress{
                    .track = dest.track,
                    .page = page,
                    .macro = entry.sourceMacro,
                },
                *clipboard,
                entry
            )) {
            clearScope(bank, dest);
            return false;
        }
    }
    return true;
}

}  // namespace

FLASHMEM bool duplicatePage(macro::MacroAutomationBankState& bank,
                            uint8_t sourceTrack,
                            uint8_t sourcePage,
                            uint8_t destTrack,
                            uint8_t destPage) {
    return duplicateScope(
        bank,
        Scope{.kind = ScopeKind::PAGE, .track = sourceTrack, .page = sourcePage},
        Scope{.kind = ScopeKind::PAGE, .track = destTrack, .page = destPage}
    );
}

FLASHMEM bool duplicateTrack(macro::MacroAutomationBankState& bank,
                             uint8_t sourceTrack,
                             uint8_t destTrack) {
    return duplicateScope(
        bank,
        Scope{.kind = ScopeKind::TRACK, .track = sourceTrack},
        Scope{.kind = ScopeKind::TRACK, .track = destTrack}
    );
}

FLASHMEM bool replacePageFromClipboard(
    macro::MacroAutomationBankState& bank,
    uint8_t destTrack,
    uint8_t destPage,
    const core::state::MacroAutomationClipboard* clipboard
) {
    return replaceScopeFromClipboard(
        bank,
        Scope{.kind = ScopeKind::PAGE, .track = destTrack, .page = destPage},
        clipboard
    );
}

FLASHMEM bool replaceTrackFromClipboard(
    macro::MacroAutomationBankState& bank,
    uint8_t destTrack,
    const core::state::MacroAutomationClipboard* clipboard
) {
    return replaceScopeFromClipboard(
        bank,
        Scope{.kind = ScopeKind::TRACK, .track = destTrack},
        clipboard
    );
}

}  // namespace core::handler::macro_structure_automation_ops
