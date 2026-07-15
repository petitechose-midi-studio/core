#pragma once

#include <cstdint>

#include "state/macro/MacroAutomationState.hpp"

namespace core::persistence::macro_automation_legacy_codec {

inline constexpr uint8_t CHUNK_VERSION_MAJOR = 1U;
inline constexpr uint8_t CHUNK_VERSION_MINOR_V14 = 4U;
inline constexpr uint8_t CHUNK_VERSION_MINOR_V15 = 5U;

inline constexpr uint32_t HEADER_SIZE = 8U;
inline constexpr uint32_t CURVE_REF_SIZE = 14U;
inline constexpr uint32_t SLOT_STATE_SIZE = CURVE_REF_SIZE * 2U + sizeof(float);
inline constexpr uint32_t ENTRY_SIZE = 4U + SLOT_STATE_SIZE;
inline constexpr uint32_t POINT_SIZE = 4U;
inline constexpr uint32_t MAX_PAYLOAD_SIZE =
    HEADER_SIZE +
    static_cast<uint32_t>(
        core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY
    ) * ENTRY_SIZE +
    static_cast<uint32_t>(
        core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY
    ) * POINT_SIZE;

[[nodiscard]] uint32_t payloadSize(uint8_t entryCount, uint16_t pointCount);

[[nodiscard]] bool validBank(
    const core::state::macro::MacroAutomationBankState& bank
);

/** Encodes the legacy writable v1.5 payload used until Gate 3 cuts authority. */
[[nodiscard]] bool encodeV15(
    const core::state::macro::MacroAutomationBankState& bank,
    uint8_t* out,
    uint32_t outCapacity,
    uint32_t& outSize
);

/** Decodes the read-only v1.4/v1.5 migration inputs transactionally. */
[[nodiscard]] bool decode(
    const uint8_t* data,
    uint32_t size,
    uint8_t versionMinor,
    core::state::macro::MacroAutomationBankState& out
);

/**
 * Allocation-free variant for a caller-owned temporary bank. The destination
 * may be partially written on failure and must never be a published live bank.
 */
[[nodiscard]] bool decodeIntoPending(
    const uint8_t* data,
    uint32_t size,
    uint8_t versionMinor,
    core::state::macro::MacroAutomationBankState& pending
);

static_assert(SLOT_STATE_SIZE == 32U);
static_assert(ENTRY_SIZE == 36U);
static_assert(POINT_SIZE == sizeof(core::state::macro::MacroPackedCurvePoint));

}  // namespace core::persistence::macro_automation_legacy_codec
