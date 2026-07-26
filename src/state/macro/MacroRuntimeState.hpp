#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/macro/MacroAutomationAddress.hpp"

namespace core::state::macro {

/**
 * Runtime-only Manual overrides keyed by logical Macro destination.
 *
 * The cache is intentionally bounded independently from durable Project graph
 * capacity. Overflow is reported and counted; it never evicts another audible
 * override.
 */
struct MacroManualOverrideState {
    static constexpr uint8_t CAPACITY = 64U;

    enum class ActivateStatus : uint8_t {
        ACTIVATED = 0,
        UPDATED,
        UNCHANGED,
        INVALID_ADDRESS,
        CAPACITY_EXHAUSTED,
    };

    struct Entry {
        bool active = false;
        MacroAutomationSlotAddress address{};
        float value = 0.0f;
    };

    struct Snapshot {
        uint32_t revision = 0;
        uint8_t entryCount = 0;
        std::array<Entry, CAPACITY> entries{};
    };

    uint8_t entryCount = 0;
    std::array<Entry, CAPACITY> entries{};
    uint32_t revision = 0;
    uint32_t rejectedActivationCount = 0;

    [[nodiscard]] const Entry* find(const MacroAutomationSlotAddress& address) const;
    [[nodiscard]] Entry* findMutable(const MacroAutomationSlotAddress& address);
    [[nodiscard]] bool activeFor(const MacroAutomationSlotAddress& address) const;
    [[nodiscard]] bool valueFor(const MacroAutomationSlotAddress& address,
                                float& outValue) const;
    [[nodiscard]] bool captureSnapshot(Snapshot& out) const;

    ActivateStatus activate(const MacroAutomationSlotAddress& address, float value);
    bool resume(const MacroAutomationSlotAddress& address);
    bool clearAddress(const MacroAutomationSlotAddress& address);
    uint8_t clearPage(uint8_t track, uint8_t page);
    /** Clears non-retained Pages and compacts retained Page addresses. */
    uint8_t compactPages(uint8_t track, uint16_t retainedPageMask);
    uint8_t clearTrack(uint8_t track);

    /** Project load/create/reset boundary. Telemetry remains cumulative. */
    void clearProjectRuntime();
    void resetTelemetry();

private:
    void noteMutation();
    void noteRejectedActivation();
};

static_assert(std::is_trivially_copyable_v<MacroManualOverrideState::Entry>);
static_assert(std::is_trivially_copyable_v<MacroManualOverrideState::Snapshot>);
static_assert(sizeof(MacroManualOverrideState) <= 1024U);
static_assert(sizeof(MacroManualOverrideState::Snapshot) <= 1024U);

}  // namespace core::state::macro
