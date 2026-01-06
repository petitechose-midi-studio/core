#pragma once

/**
 * @file MacroPagesState.hpp
 * @brief Multi-page macro configuration with persistence support
 *
 * Manages 8 pages of macro configurations. Each page stores:
 * - Page name (16 chars)
 * - CC numbers for each macro (8 bytes)
 * - MIDI channels for each macro (8 bytes)
 * - Last values for each macro (8 floats = 32 bytes)
 *
 * Total: 64 bytes per page, 512 bytes for all 8 pages.
 */

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <oc/state/Signal.hpp>

#include "config/InputIDs.hpp"

namespace core::state::macro {

static constexpr uint8_t PAGE_COUNT = 8;
static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;
static constexpr uint8_t PAGE_NAME_SIZE = 16;

/**
 * @brief Single macro configuration (CC + channel)
 */
struct MacroConfig {
    uint8_t cc = 1;       ///< MIDI CC number (0-127)
    uint8_t channel = 0;  ///< MIDI channel (0-15)
};

/**
 * @brief Complete page configuration (persisted to EEPROM)
 *
 * Layout must be exactly 64 bytes for predictable storage offsets.
 */
struct MacroPageData {
    char name[PAGE_NAME_SIZE];                      ///< Page name (16 bytes)
    std::array<uint8_t, MACRO_COUNT> cc;            ///< CC numbers (8 bytes)
    std::array<uint8_t, MACRO_COUNT> channel;       ///< Channels (8 bytes)
    std::array<float, MACRO_COUNT> values;          ///< Last values (32 bytes)

    MacroPageData() {
        std::memset(name, 0, PAGE_NAME_SIZE);
        std::strncpy(name, "Page 1", PAGE_NAME_SIZE - 1);
        cc.fill(1);
        channel.fill(0);
        values.fill(0.5f);
    }

    /// Initialize with page number
    void initDefault(uint8_t pageIndex) {
        std::memset(name, 0, PAGE_NAME_SIZE);
        snprintf(name, PAGE_NAME_SIZE, "Page %d", pageIndex + 1);

        // Default CC mapping: page 0 = CC 1-8, page 1 = CC 9-16, etc.
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            cc[i] = static_cast<uint8_t>(pageIndex * MACRO_COUNT + i + 1);
            channel[i] = 0;
            values[i] = 0.5f;
        }
    }

    /// Get config for a macro
    MacroConfig getConfig(uint8_t macroIndex) const {
        return {cc[macroIndex], channel[macroIndex]};
    }
};

static_assert(sizeof(MacroPageData) == 64, "MacroPageData must be exactly 64 bytes");

/**
 * @brief State for page selector overlay
 */
struct PageSelectorState {
    oc::state::Signal<uint8_t> selectedIndex{0};  ///< Currently highlighted page
    oc::state::Signal<bool> visible{false};       ///< Overlay visibility
};

/**
 * @brief Runtime state for macro pages
 *
 * Stores all page configurations and tracks active page.
 * activeConfigs provides quick access to current page's CC/channel mapping.
 */
struct MacroPagesState {
    /// Page selector overlay state
    PageSelectorState selector;

    /// All page data (persisted)
    std::array<MacroPageData, PAGE_COUNT> pages;

    /// Currently active page index
    uint8_t activePage = 0;

    /// Quick access to active page's configs (updated on page switch)
    std::array<MacroConfig, MACRO_COUNT> activeConfigs;

    MacroPagesState() {
        initDefaults();
    }

    /// Initialize all pages with defaults
    void initDefaults() {
        for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
            pages[i].initDefault(i);
        }
        activePage = 0;
        updateActiveConfigs();
    }

    /// Switch to a different page
    void setActivePage(uint8_t index) {
        if (index >= PAGE_COUNT) return;
        activePage = index;
        updateActiveConfigs();
    }

    /// Get active page data
    MacroPageData& activePageData() { return pages[activePage]; }
    const MacroPageData& activePageData() const { return pages[activePage]; }

    /// Get page name
    const char* pageName(uint8_t index) const {
        return (index < PAGE_COUNT) ? pages[index].name : "";
    }

    /// Update activeConfigs from current page
    void updateActiveConfigs() {
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            activeConfigs[i] = pages[activePage].getConfig(i);
        }
    }
};

}  // namespace core::state::macro
