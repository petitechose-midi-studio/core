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

#include "CoreSettings.hpp"
#include "MacroState.hpp"
#include "macro/MacroPagesState.hpp"

namespace state {

/**
 * @brief Overlay types for standalone mode
 */
enum class CoreOverlayType : uint8_t {
    NONE = 0,
    PAGE_SELECTOR,
    COUNT  // Must be last
};

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

        // Load new page values
        syncMacrosFromActivePage();
    }

    /**
     * @brief Update persistence (call from main loop)
     */
    void update(uint32_t currentTimeMs) {
        settings.update(currentTimeMs, pages);
    }

    /**
     * @brief Mark values as changed (triggers delayed save)
     */
    void onValueChanged(uint32_t currentTimeMs) {
        // Update stored value in page
        auto& pageData = pages.activePageData();
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            pageData.values[i] = macros.slots[i].value.get();
        }
        settings.markValuesDirty(currentTimeMs);
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
};

}  // namespace state
