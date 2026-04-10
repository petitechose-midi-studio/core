#pragma once

/**
 * @file MacroPagesState.hpp
 * @brief Multi-page macro configuration with persistence support
 *
 * Manages a bank of macro tracks. Each track stores:
 * - Track MIDI channel
 * - Active page index
 * - Page enabled mask
 * - 16 pages of macro configuration
 *
 * Each page stores:
 * - Page name (16 chars)
 * - CC numbers for each macro (8 bytes)
 * - Last values for each macro (8 floats = 32 bytes)
 *
 * Total: 56 bytes per page, 900 bytes per track.
 */

#include <array>
#include <cstdint>
#include <cstring>

#include <oc/state/Signal.hpp>
#include <oc/type/TextFormat.hpp>

#include <config/InputIDs.hpp>

namespace core::state::macro {

static constexpr uint8_t PAGE_COUNT = 16;
static constexpr uint8_t TRACK_COUNT = 16;
static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;
static constexpr uint8_t PAGE_NAME_SIZE = 16;

/**
 * @brief Single macro configuration (CC + track channel)
 */
struct MacroConfig {
    uint8_t cc = 0;       ///< MIDI CC number (0-127)
    uint8_t channel = 0;  ///< MIDI channel (0-15)
};

/**
 * @brief Complete page configuration (persisted)
 */
struct MacroPageData {
    char name[PAGE_NAME_SIZE];                      ///< Page name (16 bytes)
    std::array<uint8_t, MACRO_COUNT> cc;            ///< CC numbers (8 bytes)
    std::array<float, MACRO_COUNT> values;          ///< Last values (32 bytes)

    MacroPageData() {
        std::memset(name, 0, PAGE_NAME_SIZE);
        std::strncpy(name, "Page 1", PAGE_NAME_SIZE - 1);
        cc.fill(0);
        values.fill(0.5f);
    }

    /// Initialize with page number
    void initDefault(uint8_t pageIndex) {
        std::memset(name, 0, PAGE_NAME_SIZE);
        size_t pos = oc::type::text::appendString(name, PAGE_NAME_SIZE, 0, "Page ");
        pos = oc::type::text::appendUnsigned(name, PAGE_NAME_SIZE, pos, pageIndex + 1);
        oc::type::text::terminate(name, PAGE_NAME_SIZE, pos);

        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            cc[i] = static_cast<uint8_t>(pageIndex * MACRO_COUNT + i);
            values[i] = 0.5f;
        }
    }

    /// Get config for a macro
    MacroConfig getConfig(uint8_t macroIndex, uint8_t trackChannel) const {
        return {cc[macroIndex], trackChannel};
    }
};

static_assert(sizeof(MacroPageData) == 56, "MacroPageData must be exactly 56 bytes");

/**
 * @brief State for page selector overlay
 */
struct PageSelectorState {
    oc::state::Signal<uint8_t, 4> selectedIndex{0};  ///< Currently highlighted page
    oc::state::Signal<bool, 4> visible{false};       ///< Overlay visibility
};

/**
 * @brief Persisted data for one macro track
 *
 * One track carries a single MIDI channel and up to 8 macro pages.
 */
struct MacroTrackData {
    uint8_t channel = 0;          ///< Track MIDI channel (0-15)
    uint8_t activePage = 0;       ///< Active page within this track
    uint16_t enabledPageMask = 0x0001;  ///< Enabled macro pages for this track
    std::array<MacroPageData, PAGE_COUNT> pages{};

    MacroTrackData() {
        initDefaults(0);
    }

    void initDefaults(uint8_t trackIndex) {
        channel = static_cast<uint8_t>(trackIndex % 16U);
        activePage = 0;
        enabledPageMask = 0x0001;
        for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
            pages[i].initDefault(i);
        }
    }

    MacroPageData& activePageData() { return pages[activePage]; }
    const MacroPageData& activePageData() const { return pages[activePage]; }

    bool isPageEnabled(uint8_t index) const {
        if (index >= PAGE_COUNT) return false;
        return (enabledPageMask & static_cast<uint16_t>(1U << index)) != 0;
    }

    void setPageEnabled(uint8_t index, bool enabled) {
        if (index >= PAGE_COUNT) return;
        const uint16_t bit = static_cast<uint16_t>(1U << index);
        if (enabled) enabledPageMask |= bit;
        else enabledPageMask &= static_cast<uint16_t>(~bit);
    }
};

/**
 * @brief Runtime state for macro tracks/pages
 *
 * Stores all track configurations and exposes a compatibility cache for the
 * currently active track/page to avoid broad UI churn.
 */
struct MacroPagesState {
    using EnabledMaskSignal = oc::state::Signal<uint16_t, 16>;
    using TrackEnabledMaskSignal = oc::state::Signal<uint16_t, 16>;

    /// Page selector overlay state
    PageSelectorState selector;

    /// All track data (persisted)
    std::array<MacroTrackData, TRACK_COUNT> tracks;

    /// Currently active track index
    uint8_t activeTrack = 0;

    /// Compatibility mirror of the active page in the current track
    uint8_t activePage = 0;

    /// Compatibility mirror of enabled pages for the current track.
    EnabledMaskSignal enabledMask{0x0001};

    /// Runtime track enabled mask, aligned with the sequencer track model.
    TrackEnabledMaskSignal trackEnabledMask{0x01};

    /// Quick access to active page's configs (updated on page switch)
    std::array<MacroConfig, MACRO_COUNT> activeConfigs;

    MacroPagesState() {
        initDefaults();
    }

    /// Initialize all pages with defaults
    void initDefaults() {
        for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
            tracks[i].initDefaults(i);
        }
        activeTrack = 0;
        activePage = 0;
        trackEnabledMask.set(0x0001);
        syncActiveTrackCache();
        updateActiveConfigs();
    }

    void setActiveTrack(uint8_t index) {
        if (index >= TRACK_COUNT) return;
        activeTrack = index;
        syncActiveTrackCache();
        updateActiveConfigs();
    }

    /// Switch to a different page
    void setActivePage(uint8_t index) {
        if (index >= PAGE_COUNT) return;
        tracks[activeTrack].activePage = index;
        activePage = index;
        updateActiveConfigs();
    }

    MacroTrackData& activeTrackData() { return tracks[activeTrack]; }
    const MacroTrackData& activeTrackData() const { return tracks[activeTrack]; }

    /// Get active page data
    MacroPageData& activePageData() { return activeTrackData().activePageData(); }
    const MacroPageData& activePageData() const { return activeTrackData().activePageData(); }

    MacroPageData& pageData(uint8_t trackIndex, uint8_t pageIndex) {
        return tracks[trackIndex].pages[pageIndex];
    }

    const MacroPageData& pageData(uint8_t trackIndex, uint8_t pageIndex) const {
        return tracks[trackIndex].pages[pageIndex];
    }

    uint8_t activeTrackChannel() const {
        return activeTrackData().channel;
    }

    void setActiveTrackChannel(uint8_t channel) {
        activeTrackData().channel = static_cast<uint8_t>(channel % 16U);
        updateActiveConfigs();
    }

    /// Get page name
    const char* pageName(uint8_t index) const {
        return (index < PAGE_COUNT) ? activeTrackData().pages[index].name : "";
    }

    bool isPageEnabled(uint8_t index) const {
        return activeTrackData().isPageEnabled(index);
    }

    void setPageEnabled(uint8_t index, bool enabled) {
        activeTrackData().setPageEnabled(index, enabled);
        syncActiveTrackCache();
    }

    void togglePageEnabled(uint8_t index) {
        setPageEnabled(index, !isPageEnabled(index));
    }

    bool isTrackEnabled(uint8_t index) const {
        if (index >= TRACK_COUNT) return false;
        return (trackEnabledMask.get() & static_cast<uint16_t>(1U << index)) != 0;
    }

    void setTrackEnabled(uint8_t index, bool enabled) {
        if (index >= TRACK_COUNT) return;
        uint16_t mask = trackEnabledMask.get();
        const uint16_t bit = static_cast<uint16_t>(1U << index);
        if (enabled) mask |= bit;
        else mask &= static_cast<uint16_t>(~bit);
        trackEnabledMask.set(mask);
    }

    void toggleTrackEnabled(uint8_t index) {
        setTrackEnabled(index, !isTrackEnabled(index));
    }

    /// Update activeConfigs from current page
    void updateActiveConfigs() {
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            activeConfigs[i] = {
                activePageData().cc[i],
                activeTrackChannel(),
            };
        }
    }

    void syncActiveTrackCache() {
        activePage = activeTrackData().activePage;
        enabledMask.set(activeTrackData().enabledPageMask);
    }
};

}  // namespace core::state::macro
