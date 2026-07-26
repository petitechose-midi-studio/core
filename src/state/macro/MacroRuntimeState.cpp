#include "state/macro/MacroRuntimeState.hpp"

#include <cmath>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/macro/MacroAutomationDomain.hpp"

namespace core::state::macro {

namespace {

constexpr float kValueEqualityEpsilon = 0.000001f;

}  // namespace

FLASHMEM const MacroManualOverrideState::Entry* MacroManualOverrideState::find(
    const MacroAutomationSlotAddress& address
) const {
    if (!macroAutomationAddressValid(address)) return nullptr;
    const uint8_t count = entryCount > CAPACITY ? CAPACITY : entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        if (entry.active && macroAutomationAddressEquals(entry.address, address)) {
            return &entry;
        }
    }
    return nullptr;
}

FLASHMEM MacroManualOverrideState::Entry* MacroManualOverrideState::findMutable(
    const MacroAutomationSlotAddress& address
) {
    return const_cast<Entry*>(
        static_cast<const MacroManualOverrideState&>(*this).find(address)
    );
}

FLASHMEM bool MacroManualOverrideState::activeFor(
    const MacroAutomationSlotAddress& address
) const {
    return find(address) != nullptr;
}

FLASHMEM bool MacroManualOverrideState::valueFor(
    const MacroAutomationSlotAddress& address,
    float& outValue
) const {
    const auto* entry = find(address);
    if (entry == nullptr) return false;
    outValue = entry->value;
    return true;
}

FLASHMEM bool MacroManualOverrideState::captureSnapshot(Snapshot& out) const {
    // Runtime mutations and publication normally share the application thread.
    // The revision guard still makes the contract explicit and bounded for
    // consumers that publish the snapshot at a frame boundary.
    constexpr uint8_t kMaxAttempts = 2;
    for (uint8_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const uint32_t revisionBefore = revision;
        Snapshot candidate{};
        candidate.revision = revisionBefore;
        candidate.entryCount = entryCount > CAPACITY ? CAPACITY : entryCount;
        for (uint8_t i = 0; i < candidate.entryCount; ++i) {
            candidate.entries[i] = entries[i];
        }
        if (revisionBefore == revision) {
            out = candidate;
            return true;
        }
    }
    out = {};
    return false;
}

FLASHMEM MacroManualOverrideState::ActivateStatus MacroManualOverrideState::activate(
    const MacroAutomationSlotAddress& address,
    float value
) {
    if (!macroAutomationAddressValid(address)) {
        noteRejectedActivation();
        return ActivateStatus::INVALID_ADDRESS;
    }
    const float sanitized = macroAutomationClamp01(value);
    if (auto* existing = findMutable(address)) {
        if (std::fabs(existing->value - sanitized) <= kValueEqualityEpsilon) {
            return ActivateStatus::UNCHANGED;
        }
        existing->value = sanitized;
        noteMutation();
        return ActivateStatus::UPDATED;
    }
    if (entryCount >= CAPACITY) {
        noteRejectedActivation();
        return ActivateStatus::CAPACITY_EXHAUSTED;
    }
    entries[entryCount] = Entry{
        .active = true,
        .address = address,
        .value = sanitized,
    };
    entryCount = static_cast<uint8_t>(entryCount + 1U);
    noteMutation();
    return ActivateStatus::ACTIVATED;
}

FLASHMEM bool MacroManualOverrideState::resume(
    const MacroAutomationSlotAddress& address
) {
    if (!macroAutomationAddressValid(address)) return false;
    const uint8_t count = entryCount > CAPACITY ? CAPACITY : entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        if (!entries[i].active ||
            !macroAutomationAddressEquals(entries[i].address, address)) {
            continue;
        }
        for (uint8_t j = i; j + 1U < count; ++j) {
            entries[j] = entries[j + 1U];
        }
        entries[count - 1U] = {};
        entryCount = static_cast<uint8_t>(count - 1U);
        noteMutation();
        return true;
    }
    return false;
}

FLASHMEM bool MacroManualOverrideState::clearAddress(
    const MacroAutomationSlotAddress& address
) {
    return resume(address);
}

FLASHMEM uint8_t MacroManualOverrideState::clearPage(uint8_t track, uint8_t page) {
    if (track >= TRACK_COUNT || page >= PAGE_COUNT) return 0;
    const uint8_t count = entryCount > CAPACITY ? CAPACITY : entryCount;
    uint8_t write = 0;
    for (uint8_t read = 0; read < count; ++read) {
        const auto& entry = entries[read];
        if (entry.active && entry.address.track == track && entry.address.page == page) {
            continue;
        }
        if (write != read) entries[write] = entry;
        ++write;
    }
    const uint8_t removed = static_cast<uint8_t>(count - write);
    if (removed == 0) return 0;
    for (uint8_t i = write; i < count; ++i) entries[i] = {};
    entryCount = write;
    noteMutation();
    return removed;
}

FLASHMEM uint8_t MacroManualOverrideState::compactPages(
    uint8_t track,
    uint16_t retainedPageMask
) {
    if (track >= TRACK_COUNT || retainedPageMask == 0U) return 0U;
    const uint8_t count = entryCount > CAPACITY ? CAPACITY : entryCount;
    uint8_t write = 0U;
    uint8_t affected = 0U;
    for (uint8_t read = 0U; read < count; ++read) {
        auto entry = entries[read];
        if (entry.active && entry.address.track == track) {
            const uint16_t pageBit = static_cast<uint16_t>(
                1U << entry.address.page
            );
            if ((retainedPageMask & pageBit) == 0U) {
                ++affected;
                continue;
            }
            uint8_t compactedPage = 0U;
            for (uint8_t page = 0U; page < entry.address.page; ++page) {
                if ((retainedPageMask & static_cast<uint16_t>(1U << page)) !=
                    0U) {
                    ++compactedPage;
                }
            }
            if (compactedPage != entry.address.page) {
                entry.address.page = compactedPage;
                ++affected;
            }
        }
        entries[write++] = entry;
    }
    if (affected == 0U) return 0U;
    for (uint8_t index = write; index < count; ++index) entries[index] = {};
    entryCount = write;
    noteMutation();
    return affected;
}

FLASHMEM uint8_t MacroManualOverrideState::clearTrack(uint8_t track) {
    if (track >= TRACK_COUNT) return 0;
    const uint8_t count = entryCount > CAPACITY ? CAPACITY : entryCount;
    uint8_t write = 0;
    for (uint8_t read = 0; read < count; ++read) {
        const auto& entry = entries[read];
        if (entry.active && entry.address.track == track) continue;
        if (write != read) entries[write] = entry;
        ++write;
    }
    const uint8_t removed = static_cast<uint8_t>(count - write);
    if (removed == 0) return 0;
    for (uint8_t i = write; i < count; ++i) entries[i] = {};
    entryCount = write;
    noteMutation();
    return removed;
}

FLASHMEM void MacroManualOverrideState::clearProjectRuntime() {
    if (entryCount == 0) return;
    entryCount = 0;
    entries = {};
    noteMutation();
}

FLASHMEM void MacroManualOverrideState::resetTelemetry() {
    rejectedActivationCount = 0;
}

FLASHMEM void MacroManualOverrideState::noteMutation() {
    ++revision;
}

FLASHMEM void MacroManualOverrideState::noteRejectedActivation() {
    if (rejectedActivationCount < std::numeric_limits<uint32_t>::max()) {
        ++rejectedActivationCount;
    }
}

}  // namespace core::state::macro
