#pragma once

#include <cstddef>
#include <cstdint>

#include "DataManagerCatalog.hpp"
#include "macro/MacroPagesState.hpp"

namespace core::state::core_settings {

namespace layout {

constexpr uint32_t MAGIC = 0x4D435354;
constexpr uint8_t VERSION = 3;

constexpr uint32_t ADDR_MAGIC = 0x0000;
constexpr uint32_t ADDR_VERSION = 0x0004;
constexpr uint32_t ADDR_ACTIVE_PAGE = 0x0005;
constexpr uint32_t ADDR_RESERVED = 0x0006;
constexpr uint32_t ADDR_PAGES = 0x0010;

constexpr uint32_t ADDR_SYNC_MODE = ADDR_RESERVED;
constexpr uint32_t ADDR_SYNC_FOLLOW_TRANSPORT = ADDR_RESERVED + 1;
constexpr uint32_t ADDR_SYNC_AUTO_FALLBACK_MS = ADDR_RESERVED + 2;
constexpr uint32_t ADDR_SYNC_AUTO_LOCK_CLOCKS = ADDR_RESERVED + 4;

constexpr uint32_t ADDR_SHORTCUT_MACRO_LEFT = ADDR_RESERVED + 5;
constexpr uint32_t ADDR_SHORTCUT_MACRO_RIGHT = ADDR_RESERVED + 6;
constexpr uint32_t ADDR_SHORTCUT_SEQ_LEFT = ADDR_RESERVED + 7;
constexpr uint32_t ADDR_SHORTCUT_SEQ_RIGHT = ADDR_RESERVED + 8;
static_assert(ADDR_SHORTCUT_SEQ_RIGHT < ADDR_PAGES);

constexpr uint8_t DEFAULT_SHORTCUT_MACRO_LEFT =
    static_cast<uint8_t>(DEFAULT_MACRO_SHORTCUT_LEFT);
constexpr uint8_t DEFAULT_SHORTCUT_MACRO_RIGHT =
    static_cast<uint8_t>(DEFAULT_MACRO_SHORTCUT_RIGHT);
constexpr uint8_t DEFAULT_SHORTCUT_SEQ_LEFT =
    static_cast<uint8_t>(DEFAULT_SEQ_SHORTCUT_LEFT);
constexpr uint8_t DEFAULT_SHORTCUT_SEQ_RIGHT =
    static_cast<uint8_t>(DEFAULT_SEQ_SHORTCUT_RIGHT);

constexpr size_t MACRO_PAGE_SIZE = sizeof(macro::MacroPageData);
static_assert(MACRO_PAGE_SIZE == 64, "Page size must be 64 bytes");

static constexpr uint32_t OFF_NAME = static_cast<uint32_t>(offsetof(macro::MacroPageData, name));
static constexpr uint32_t OFF_CC = static_cast<uint32_t>(offsetof(macro::MacroPageData, cc));
static constexpr uint32_t OFF_CHANNEL = static_cast<uint32_t>(offsetof(macro::MacroPageData, channel));
static constexpr uint32_t OFF_VALUES = static_cast<uint32_t>(offsetof(macro::MacroPageData, values));

static_assert(OFF_NAME == 0, "Unexpected MacroPageData layout");
static_assert(OFF_CC == 16, "Unexpected MacroPageData layout");
static_assert(OFF_CHANNEL == 24, "Unexpected MacroPageData layout");
static_assert(OFF_VALUES == 32, "Unexpected MacroPageData layout");

inline constexpr uint32_t pageOffset(uint8_t pageIndex) {
    return ADDR_PAGES + pageIndex * MACRO_PAGE_SIZE;
}

inline constexpr uint32_t valueOffset(uint8_t pageIndex, uint8_t macroIndex) {
    return pageOffset(pageIndex) + OFF_VALUES + macroIndex * sizeof(float);
}

inline constexpr uint32_t ccOffset(uint8_t pageIndex, uint8_t macroIndex) {
    return pageOffset(pageIndex) + OFF_CC + macroIndex;
}

inline constexpr uint32_t channelOffset(uint8_t pageIndex, uint8_t macroIndex) {
    return pageOffset(pageIndex) + OFF_CHANNEL + macroIndex;
}

}  // namespace layout

}  // namespace core::state::core_settings
