#pragma once

#include <array>
#include <cstdint>

#include "state/macro/MacroPagesState.hpp"

namespace core::persistence::macro_track_codec {

inline constexpr uint16_t MACRO_PAGE_RESERVED_SIZE = 3;
inline constexpr uint16_t MACRO_PAGE_PAYLOAD_SIZE =
    core::state::macro::PAGE_NAME_SIZE +
    core::state::macro::MACRO_COUNT +
    static_cast<uint16_t>(sizeof(float) * core::state::macro::MACRO_COUNT) +
    1U +
    MACRO_PAGE_RESERVED_SIZE;
inline constexpr uint16_t MACRO_TRACK_PAYLOAD_SIZE =
    3U + static_cast<uint16_t>(core::state::macro::PAGE_COUNT * MACRO_PAGE_PAYLOAD_SIZE);
inline constexpr uint16_t MACRO_TRACK_BANK_PAYLOAD_SIZE =
    4U + static_cast<uint16_t>(core::state::macro::TRACK_COUNT * MACRO_TRACK_PAYLOAD_SIZE);

static_assert(MACRO_PAGE_PAYLOAD_SIZE == 60, "Unexpected macro page payload size");
static_assert(MACRO_TRACK_PAYLOAD_SIZE == 963, "Unexpected macro track payload size");
static_assert(MACRO_TRACK_BANK_PAYLOAD_SIZE == 15412, "Unexpected macro track bank payload size");

bool encodeTrackBankPayload(
    const std::array<core::state::macro::MacroTrackData, core::state::macro::TRACK_COUNT>& tracks,
    uint16_t enabledTrackMask,
    uint8_t activeTrack,
    uint8_t* out,
    uint32_t capacity
);

/**
 * Decode directly into caller-owned scratch storage.
 *
 * The destination may be partially written when false is returned. Callers
 * that expose live state must therefore decode into disposable storage first.
 */
bool decodeTrackBankPayloadInto(
    const uint8_t* data,
    uint32_t size,
    std::array<core::state::macro::MacroTrackData, core::state::macro::TRACK_COUNT>& tracks,
    uint16_t& enabledTrackMask,
    uint8_t& activeTrack
);

bool encodePagesPayload(const core::state::macro::MacroPagesState& pages,
                        uint8_t* out,
                        uint32_t capacity);

bool applyPagesPayload(const uint8_t* data,
                       uint32_t size,
                       core::state::macro::MacroPagesState& pages);

}  // namespace core::persistence::macro_track_codec
