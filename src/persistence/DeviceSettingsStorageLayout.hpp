#pragma once

#include <cstdint>

namespace core::persistence::device_settings {

/**
 * Byte-level layout for DeviceSettingsStore storage.
 *
 * Offsets and VERSION define the exact current device-settings format.
 * Pre-V1 builds accept this version only.
 */
namespace layout {

constexpr uint32_t MAGIC = 0x4D435354;
constexpr uint8_t VERSION = 4;

constexpr uint32_t ADDR_MAGIC = 0x0000;
constexpr uint32_t ADDR_VERSION = 0x0004;
constexpr uint32_t ADDR_RESERVED = 0x0005;

constexpr uint32_t ADDR_SYNC_MODE = ADDR_RESERVED;
constexpr uint32_t ADDR_SYNC_FOLLOW_TRANSPORT = ADDR_RESERVED + 1;
constexpr uint32_t ADDR_SYNC_AUTO_FALLBACK_MS = ADDR_RESERVED + 2;
constexpr uint32_t ADDR_SYNC_AUTO_LOCK_CLOCKS = ADDR_RESERVED + 4;
constexpr uint32_t ADDR_NOTE_OCTAVE_CONVENTION = ADDR_RESERVED + 5;

constexpr uint32_t STORAGE_END = ADDR_NOTE_OCTAVE_CONVENTION + 1;

}  // namespace layout

}  // namespace core::persistence::device_settings
