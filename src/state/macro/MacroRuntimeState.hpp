#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/macro/MacroAutomationState.hpp"

namespace core::state::macro {

/**
 * Runtime-only Manual overrides keyed by the durable V1 Slot address.
 *
 * Capacity is exactly the maximum number of sparse Macro source entries. A
 * Slot without a stored computed source never needs a Manual entry. Overflow
 * is reported and counted; it never evicts another audible override.
 */
struct MacroManualOverrideState {
    static constexpr uint8_t CAPACITY = MACRO_AUTOMATION_SLOT_CAPACITY;

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
    uint8_t clearTrack(uint8_t track);

    /** Project load/create/reset boundary. Telemetry remains cumulative. */
    void clearProjectRuntime();
    void resetTelemetry();

private:
    void noteMutation();
    void noteRejectedActivation();
};

static_assert(MacroManualOverrideState::CAPACITY == MACRO_AUTOMATION_SLOT_CAPACITY);
static_assert(std::is_trivially_copyable_v<MacroManualOverrideState::Entry>);
static_assert(std::is_trivially_copyable_v<MacroManualOverrideState::Snapshot>);
static_assert(sizeof(MacroManualOverrideState) <= 1024U);
static_assert(sizeof(MacroManualOverrideState::Snapshot) <= 1024U);

}  // namespace core::state::macro
