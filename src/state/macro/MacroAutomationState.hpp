#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/macro/MacroAutomationDomain.hpp"
#include "state/macro/MacroConstants.hpp"

namespace core::state::macro {

static constexpr uint8_t MACRO_AUTOMATION_SLOT_CAPACITY = 64;

struct MacroAutomationSlotAddress {
    uint8_t track = 0;
    uint8_t page = 0;
    uint8_t macro = 0;
};

struct MacroAutomationSlotEntry {
    bool active = false;
    MacroAutomationSlotAddress address{};
    MacroAutomationSlotState state{};
};

struct MacroAutomationBankState {
    uint8_t entryCount = 0;
    std::array<MacroAutomationSlotEntry, MACRO_AUTOMATION_SLOT_CAPACITY> entries{};
    MacroAutomationPointPool pointPool{};

    void clear();
};

enum class MacroAutomationConversionStatus : uint8_t {
    READY = 0,
    OVERWRITE_REQUIRED,
    INVALID_ADDRESS,
    INVALID_BANK,
    NO_AUTOMATION,
    POINT_POOL_EXHAUSTED,
    STALE_PLAN,
};

/**
 * Immutable result of an Automation -> Modulation preflight.
 *
 * Fingerprints deliberately exclude pool offsets so unrelated compaction does
 * not invalidate a semantically identical source. Commit recomputes capacity
 * and both fingerprints before the first write.
 */
struct MacroAutomationConversionPlan {
    MacroAutomationConversionStatus status =
        MacroAutomationConversionStatus::INVALID_ADDRESS;
    MacroAutomationSlotAddress address{};
    MacroAutomationConversionPolicy policy = MacroAutomationConversionPolicy::MEAN;
    float reference = 0.0f;
    float normalizationAmplitude = 0.0f;
    float expectedStaticBase = 0.0f;
    uint16_t pointCount = 0;
    uint16_t reclaimablePointCount = 0;
    uint16_t freePointCount = 0;
    uint32_t sourceFingerprint = 0;
    uint32_t targetFingerprint = 0;
    bool overwritesModulation = false;

    bool actionable() const {
        return status == MacroAutomationConversionStatus::READY ||
               status == MacroAutomationConversionStatus::OVERWRITE_REQUIRED;
    }
};

static_assert(std::is_trivially_copyable_v<MacroAutomationSlotAddress>);
static_assert(std::is_trivially_copyable_v<MacroAutomationSlotEntry>);
static_assert(std::is_trivially_copyable_v<MacroAutomationPointPool>);
static_assert(std::is_trivially_copyable_v<MacroAutomationBankState>);

bool macroAutomationAddressValid(const MacroAutomationSlotAddress& address);
bool macroAutomationAddressEquals(const MacroAutomationSlotAddress& lhs,
                                  const MacroAutomationSlotAddress& rhs);

const MacroAutomationSlotState* macroAutomationFindSlot(
    const MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
);

MacroAutomationSlotState* macroAutomationFindMutableSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
);

MacroAutomationSlotState* macroAutomationGetOrCreateSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
);

bool macroAutomationClearSlot(MacroAutomationBankState& bank,
                              const MacroAutomationSlotAddress& address);
bool macroAutomationClearPage(MacroAutomationBankState& bank,
                              uint8_t track,
                              uint8_t page);
bool macroAutomationClearTrack(MacroAutomationBankState& bank,
                               uint8_t track);

bool macroAutomationSlotHasContent(const MacroAutomationSlotState& state);
uint16_t macroAutomationStoredPointCount(const MacroAutomationCurveRef& curve,
                                         const MacroAutomationPointPool& pool);
uint16_t macroAutomationStoredPointCount(const MacroAutomationSlotState& state,
                                         const MacroAutomationPointPool& pool);
/**
 * Validates every source invariant required by the slot-copy mutations.
 * A false result is detected before destination pool compaction or slot writes.
 */
bool macroAutomationSlotStateValidForMutation(
    const MacroAutomationSlotState& state,
    const MacroAutomationPointPool& pool
);
void macroAutomationCompactPool(MacroAutomationBankState& bank);
// `slot` must be owned by `bank`; compaction rewrites pool offsets through bank entries.
bool macroAutomationAssignAutomation(MacroAutomationBankState& bank,
                                     MacroAutomationSlotState& slot,
                                     const MacroAutomationLane& lane);
// `slot` must be owned by `bank`; compaction rewrites pool offsets through bank entries.
bool macroAutomationAssignModulation(MacroAutomationBankState& bank,
                                     MacroAutomationSlotState& slot,
                                     const MacroModulationShape& shape);
void macroAutomationClearAutomation(MacroAutomationBankState& bank,
                                    MacroAutomationSlotState& slot);
void macroAutomationClearModulation(MacroAutomationBankState& bank,
                                    MacroAutomationSlotState& slot);
MacroAutomationConversionPlan macroAutomationPreflightConversion(
    const MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address,
    MacroAutomationConversionPolicy policy,
    float currentStaticBase);
/**
 * Applies a still-current plan atomically and updates `staticBase` last.
 * Existing Modulation requires explicit overwrite confirmation.
 */
bool macroAutomationApplyConversion(MacroAutomationBankState& bank,
                                    float& staticBase,
                                    const MacroAutomationConversionPlan& plan,
                                    bool overwriteConfirmed);
bool macroAutomationCopySlotState(MacroAutomationPointPool& destPool,
                                  MacroAutomationSlotState& dest,
                                  const MacroAutomationPointPool& sourcePool,
                                  const MacroAutomationSlotState& source);
bool macroAutomationCopySlotState(MacroAutomationBankState& destBank,
                                   MacroAutomationSlotState& dest,
                                   const MacroAutomationPointPool& sourcePool,
                                   const MacroAutomationSlotState& source);
/** Replaces only Automation, preserving target Modulation and Depth. */
bool macroAutomationCopyAutomationState(
    MacroAutomationBankState& destBank,
    MacroAutomationSlotState& dest,
    const MacroAutomationPointPool& sourcePool,
    const MacroAutomationSlotState& source
);
/** Replaces only Modulation and Depth, preserving target Automation. */
bool macroAutomationCopyModulationState(
    MacroAutomationBankState& destBank,
    MacroAutomationSlotState& dest,
    const MacroAutomationPointPool& sourcePool,
    const MacroAutomationSlotState& source
);

}  // namespace core::state::macro
