#pragma once

#include <cstdint>

namespace core::state::core_settings {

/**
 * Byte-level layout for CoreSettings storage.
 *
 * Offsets and VERSION define the compatibility contract for MIDI sync and
 * shared Track state.
 */
namespace layout {

constexpr uint32_t MAGIC = 0x4D435354;
constexpr uint8_t VERSION = 2;

constexpr uint32_t ADDR_MAGIC = 0x0000;
constexpr uint32_t ADDR_VERSION = 0x0004;
constexpr uint32_t ADDR_RESERVED = 0x0005;

constexpr uint32_t ADDR_SYNC_MODE = ADDR_RESERVED;
constexpr uint32_t ADDR_SYNC_FOLLOW_TRANSPORT = ADDR_RESERVED + 1;
constexpr uint32_t ADDR_SYNC_AUTO_FALLBACK_MS = ADDR_RESERVED + 2;
constexpr uint32_t ADDR_SYNC_AUTO_LOCK_CLOCKS = ADDR_RESERVED + 4;

constexpr uint32_t ADDR_SHARED_TRACK_ENABLED_MASK = ADDR_RESERVED + 5;
constexpr uint32_t ADDR_SHARED_TRACK_ACTIVE = ADDR_RESERVED + 7;

// Bytes ADDR_RESERVED + 8..11 belonged to retired fixed-slot shortcuts.
// Keep them reserved so the feature removal cannot alter the serialized
// settings layout or silently reinterpret existing controller bytes.
constexpr uint32_t RETIRED_FIXED_SLOT_BYTES_BEGIN = ADDR_RESERVED + 8;
constexpr uint32_t RETIRED_FIXED_SLOT_BYTES_END = ADDR_RESERVED + 12;
constexpr uint32_t STORAGE_END = RETIRED_FIXED_SLOT_BYTES_END;
static_assert(RETIRED_FIXED_SLOT_BYTES_BEGIN < STORAGE_END);

constexpr uint16_t DEFAULT_SHARED_TRACK_ENABLED_MASK = 0x0001;
constexpr uint8_t DEFAULT_SHARED_TRACK_ACTIVE = 0;

}  // namespace layout

}  // namespace core::state::core_settings
