#pragma once

#include <cstdint>

#include "DataManagerCatalog.hpp"
namespace core::state::core_settings {

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

constexpr uint32_t ADDR_SHORTCUT_MACRO_LEFT = ADDR_RESERVED + 8;
constexpr uint32_t ADDR_SHORTCUT_MACRO_RIGHT = ADDR_RESERVED + 9;
constexpr uint32_t ADDR_SHORTCUT_SEQ_LEFT = ADDR_RESERVED + 10;
constexpr uint32_t ADDR_SHORTCUT_SEQ_RIGHT = ADDR_RESERVED + 11;
constexpr uint32_t STORAGE_END = ADDR_SHORTCUT_SEQ_RIGHT + 1;
static_assert(ADDR_SHORTCUT_SEQ_RIGHT < STORAGE_END);

constexpr uint16_t DEFAULT_SHARED_TRACK_ENABLED_MASK = 0x0001;
constexpr uint8_t DEFAULT_SHARED_TRACK_ACTIVE = 0;

constexpr uint8_t DEFAULT_SHORTCUT_MACRO_LEFT =
    static_cast<uint8_t>(DEFAULT_MACRO_SHORTCUT_LEFT);
constexpr uint8_t DEFAULT_SHORTCUT_MACRO_RIGHT =
    static_cast<uint8_t>(DEFAULT_MACRO_SHORTCUT_RIGHT);
constexpr uint8_t DEFAULT_SHORTCUT_SEQ_LEFT =
    static_cast<uint8_t>(DEFAULT_SEQ_SHORTCUT_LEFT);
constexpr uint8_t DEFAULT_SHORTCUT_SEQ_RIGHT =
    static_cast<uint8_t>(DEFAULT_SEQ_SHORTCUT_RIGHT);

}  // namespace layout

}  // namespace core::state::core_settings
