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
 * - ExclusiveVisibilityStack: Overlay visibility management
 */

#include <memory>

#include <oc/hal/IStorageBackend.hpp>
#include <oc/state/AutoPersistIncremental.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "CoreSettings.hpp"
#include "MacroEditState.hpp"
#include "MacroState.hpp"
#include "../ui/OverlayTypes.hpp"
#include "StatusBarState.hpp"
#include "macro/MacroPagesState.hpp"

namespace core::state {

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
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType> overlays;

    /// Status bar state (TopBar + TransportBar)
    StatusBarState statusBar;

    /// Macro edit overlay state
    MacroEditState macroEdit;

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
        overlays.registerItem(core::ui::OverlayType::PAGE_SELECTOR, pages.selector.visible);
        overlays.registerItem(core::ui::OverlayType::MACRO_EDIT, macroEdit.visible);

        // Setup auto-persistence for macro values
        auto_persist_ = std::make_unique<oc::state::AutoPersistIncremental<MACRO_COUNT>>(
            [this](uint8_t i) {
                float value = macros.slots[i].value.get();
                pages.activePageData().values[i] = value;
                settings.saveValue(pages.activePage, i, value);
            },
            [this]() { settings.commit(); },
            CoreSettings::VALUE_SAVE_DELAY_MS
        );

        // Watch each macro value signal
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            auto_persist_->watchAt(i, macros.slots[i].value);
        }
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

            // Restore value from page (displayValue updates automatically)
            macros.slots[i].value.set(pageData.values[i]);
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
     * Updates runtime state. Persistence and displayValue update automatically.
     *
     * @param index Macro index (0-7)
     * @param value Normalized value [0.0, 1.0]
     */
    void setMacroValue(uint8_t index, float value) {
        if (index >= MACRO_COUNT) return;
        macros.slots[index].value.set(value);
        // AutoPersistIncremental handles dirty tracking via signal subscription
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
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Update persistence (call from main loop)
     *
     * Saves dirty values incrementally after debounce timeout.
     */
    void update() {
        auto_persist_->update();
    }

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset() {
        settings.factoryReset();
        pages.initDefaults();
        syncMacrosFromActivePage();
        settings.saveAll(pages);
        macroEdit.reset();
        overlays.hideAll();
    }

    /**
     * @brief Flush any pending dirty values immediately
     */
    void flush() {
        auto_persist_->flush();
    }

private:
    /// Auto-persistence for macro values (watches signals, saves on debounce)
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> auto_persist_;
};

}  // namespace core::state
