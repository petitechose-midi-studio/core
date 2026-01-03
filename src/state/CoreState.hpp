#pragma once

/**
 * @file CoreState.hpp
 * @brief Global state aggregate for standalone mode
 *
 * CoreState lives at application level (in main.cpp) and survives
 * context switches. This allows state persistence across context
 * activate/deactivate cycles.
 *
 * Includes:
 * - MacroState: Runtime values and labels for 8 macro slots
 * - MacroPagesState: 8 pages of macro configurations (CC, channel, values)
 * - CoreSettings: Persistence manager for EEPROM storage
 * - OverlayManager: Overlay visibility management
 */

#include <oc/hal/IStorageBackend.hpp>
#include <oc/state/OverlayManager.hpp>
#include <oc/time/Time.hpp>

#include "CoreSettings.hpp"
#include "MacroState.hpp"
#include "OverlayTypes.hpp"
#include "StatusBarState.hpp"
#include "macro/MacroPagesState.hpp"

namespace state {

/**
 * @brief Global state container for standalone mode
 *
 * Unlike BitwigContext which owns its state internally,
 * StandaloneContext receives a reference to CoreState.
 * This allows state to survive context switches.
 */
struct CoreState {
    /// Runtime macro state (8 slots with values, labels)
    MacroState macros;

    /// Multi-page configuration (CC, channel, stored values)
    macro::MacroPagesState pages;

    /// Persistence manager
    CoreSettings settings;

    /// Overlay visibility manager
    oc::state::OverlayManager<CoreOverlayType> overlays;

    /// Status bar state (TopBar + TransportBar)
    StatusBarState statusBar;

    /**
     * @brief Construct with storage backend
     * @param storage EEPROM or other storage backend
     */
    explicit CoreState(oc::hal::IStorageBackend& storage)
        : settings(storage) {
        // Load persisted settings
        settings.load(pages);

        // Sync runtime macros with active page
        syncMacrosFromActivePage();

        // Register overlay signals
        overlays.registerOverlay(CoreOverlayType::PAGE_SELECTOR, pages.selector.visible);
    }

    // Non-copyable, non-movable
    CoreState(const CoreState&) = delete;
    CoreState& operator=(const CoreState&) = delete;
    CoreState(CoreState&&) = delete;
    CoreState& operator=(CoreState&&) = delete;

    /**
     * @brief Sync runtime macro state from active page config
     */
    void syncMacrosFromActivePage() {
        const auto& pageData = pages.activePageData();
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            // Set label from page name pattern or use page's stored label
            char label[16];
            snprintf(label, sizeof(label), "Macro %d", i + 1);
            macros.slots[i].label.set(label);

            // Restore value from page
            macros.slots[i].value.set(pageData.values[i]);
            macros.slots[i].updateDisplayValue();
        }
    }

    /**
     * @brief Switch to a different page
     */
    void switchToPage(uint8_t pageIndex) {
        if (pageIndex >= macro::PAGE_COUNT) return;

        // Save current values to old page
        auto& oldPage = pages.activePageData();
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            oldPage.values[i] = macros.slots[i].value.get();
        }

        // Switch page
        pages.setActivePage(pageIndex);
        settings.saveActivePage(pageIndex);

        // Update status bar
        char pageName[16];
        snprintf(pageName, sizeof(pageName), "Page %d", pageIndex + 1);
        statusBar.pageName.set(pageName);

        // Load new page values
        syncMacrosFromActivePage();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Macro Accessors (encapsulated access for handlers)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set macro value (user-initiated change)
     *
     * Updates runtime state, display value, and marks for persistence.
     * Use this for encoder/MIDI input, NOT for page load.
     *
     * @param index Macro index (0-7)
     * @param value Normalized value [0.0, 1.0]
     */
    void setMacroValue(uint8_t index, float value) {
        if (index >= MACRO_COUNT) return;
        macros.slots[index].value.set(value);
        macros.slots[index].updateDisplayValue();
        markDirty(index);
    }

    /**
     * @brief Get current macro value
     * @param index Macro index (0-7)
     * @return Normalized value [0.0, 1.0]
     */
    float getMacroValue(uint8_t index) const {
        if (index >= MACRO_COUNT) return 0.0f;
        return macros.slots[index].value.get();
    }

    /**
     * @brief Get macro MIDI configuration (CC, channel)
     * @param index Macro index (0-7)
     * @return Config for active page
     */
    const macro::MacroConfig& getMacroConfig(uint8_t index) const {
        static const macro::MacroConfig defaultConfig{};
        if (index >= MACRO_COUNT) return defaultConfig;
        return pages.activeConfigs[index];
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Dirty Tracking
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Mark a specific macro index as dirty (needs persistence)
     *
     * Called automatically by setMacroValue().
     * Only call directly if you modify state without using setMacroValue().
     *
     * @param index Macro index (0-7)
     */
    void markDirty(uint8_t index) {
        if (index >= MACRO_COUNT) return;
        dirtyMask_ |= (1 << index);
        if (dirtyTimestamp_ == 0) {
            dirtyTimestamp_ = oc::time::millis();
        }
    }

    /**
     * @brief Update persistence (call from main loop)
     *
     * Saves dirty values incrementally after timeout.
     */
    void update() {
        if (dirtyMask_ == 0) return;

        uint32_t now = oc::time::millis();
        if ((now - dirtyTimestamp_) < CoreSettings::VALUE_SAVE_DELAY_MS) return;

        saveDirtyValues();
    }

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset() {
        settings.factoryReset();
        pages.initDefaults();
        syncMacrosFromActivePage();
        settings.saveAll(pages);
        overlays.hideAll();
    }

    /**
     * @brief Flush any pending dirty values immediately
     */
    void flush() {
        if (dirtyMask_ == 0) return;
        saveDirtyValues();
    }

private:
    uint8_t dirtyMask_ = 0;       ///< Bitfield: which macro indices need saving
    uint32_t dirtyTimestamp_ = 0; ///< When first change occurred (for timeout)

    /**
     * @brief Save all dirty values to storage and reset dirty state
     */
    void saveDirtyValues() {
        auto& pageData = pages.activePageData();
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            if (dirtyMask_ & (1 << i)) {
                float value = macros.slots[i].value.get();
                pageData.values[i] = value;
                settings.saveValue(pages.activePage, i, value);
            }
        }
        settings.commit();
        dirtyMask_ = 0;
        dirtyTimestamp_ = 0;
    }
};

}  // namespace state
