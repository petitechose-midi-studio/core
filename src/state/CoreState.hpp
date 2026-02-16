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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>

#include <oc/interface/IStorage.hpp>
#include <oc/state/AutoPersistIncremental.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "CoreSettings.hpp"
#include "MacroEditState.hpp"
#include "MacroState.hpp"
#include "../ui/OverlayTypes.hpp"
#include "../ui/ViewTypes.hpp"
#include "StatusBarState.hpp"
#include "macro/MacroPagesState.hpp"
#include "sequencer/SequencerState.hpp"

namespace core::state {

/**
 * @brief State for top-level view selector overlay
 */
struct ViewSelectorState {
    oc::state::Signal<int> selectedIndex{0};
    oc::state::Signal<bool> visible{false};

    void reset() {
        selectedIndex.set(0);
        visible.set(false);
    }
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

    /// Bumps on any config/page change (for UI refresh)
    oc::state::Signal<uint32_t> configRevision{0};

    /// Persistence manager
    CoreSettings settings;

    /// Overlay visibility manager
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType> overlays;

    /// Active top-level view
    oc::state::Signal<core::ui::ViewType> activeView{core::ui::ViewType::MACRO};

    /// Sequencer UI-first state
    sequencer::SequencerState sequencer;

    /// View selector overlay state
    ViewSelectorState viewSelector;

    /// Status bar state (TopBar + TransportBar)
    StatusBarState statusBar;

    /// Macro edit overlay state
    MacroEditState macroEdit;

    /**
     * @brief Construct with storage backend
     * @param storage EEPROM or other storage backend
     */
    explicit CoreState(oc::interface::IStorage& storage)
        : settings(storage) {
        // Load persisted settings
        settings.load(pages);

        // Reflect loaded page name in UI state
        statusBar.pageName.set(pages.activePageData().name);

        // Sync runtime macros with active page
        syncMacrosFromActivePage();

        // Register overlay signals
        overlays.registerItem(core::ui::OverlayType::PAGE_SELECTOR, pages.selector.visible);
        overlays.registerItem(core::ui::OverlayType::MACRO_EDIT, macroEdit.visible);
        overlays.registerItem(core::ui::OverlayType::VIEW_SELECTOR, viewSelector.visible);

        overlays.registerItem(core::ui::OverlayType::SEQ_PATTERN_CONFIG, sequencer.patternConfig.visible);
        overlays.registerItem(core::ui::OverlayType::SEQ_STEP_EDIT, sequencer.stepEdit.visible);
        overlays.registerItem(core::ui::OverlayType::SEQ_PROPERTY_SELECTOR, sequencer.propertySelector.visible);

        // Setup auto-persistence for macro values
        auto_persist_ = std::make_unique<oc::state::AutoPersistIncremental<MACRO_COUNT>>(
            [this](uint8_t i) {
                float value = macros.slots[i].value.get();

                // Avoid redundant writes (e.g., when loading values from storage/page sync).
                auto& page = pages.activePageData();
                if (page.values[i] == value) return;
                page.values[i] = value;
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
            // Default labels (currently not per-page).
            char label[16];
            snprintf(label, sizeof(label), "Macro %d", i + 1);
            macros.slots[i].label.set(label);

            // Restore value from page (displayValue updates automatically)
            macros.slots[i].value.set(std::clamp(pageData.values[i], 0.0f, 1.0f));
        }
    }

    /**
     * @brief Switch to a different page
     */
    void switchToPage(uint8_t pageIndex) {
        if (pageIndex >= macro::PAGE_COUNT) return;

        // Ensure any pending value writes are committed to the current page
        // before switching the active page.
        flush();

        // Switch page
        pages.setActivePage(pageIndex);
        settings.saveActivePage(pageIndex);
        settings.commit();

        // Notify UI that config changed
        configRevision.set(configRevision.get() + 1);

        // Update status bar from persisted page name
        statusBar.pageName.set(pages.activePageData().name);

        // Load new page values
        syncMacrosFromActivePage();
    }

    /**
     * @brief Set macro MIDI configuration for the active page
     *
     * Single source of truth: updates page data, derived active configs, and persistence.
     */
    void setMacroConfig(uint8_t index, uint8_t channel, uint8_t cc) {
        if (index >= MACRO_COUNT) return;
        if (channel > 15 || cc > 127) return;

        auto& page = pages.activePageData();
        page.channel[index] = channel;
        page.cc[index] = cc;
        pages.updateActiveConfigs();

        settings.saveChannel(pages.activePage, index, channel);
        settings.saveCC(pages.activePage, index, cc);
        settings.commit();

        configRevision.set(configRevision.get() + 1);
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
        macros.slots[index].value.set(std::clamp(value, 0.0f, 1.0f));
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
        if (auto_persist_) {
            auto_persist_->update();
        }
    }

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset() {
        settings.factoryReset();
        pages.initDefaults();
        syncMacrosFromActivePage();
        settings.saveAll(pages);
        statusBar.pageName.set(pages.activePageData().name);
        macroEdit.reset();
        viewSelector.reset();
        sequencer.reset();
        activeView.set(core::ui::ViewType::MACRO);
        overlays.hideAll();
        configRevision.set(configRevision.get() + 1);
    }

    /**
     * @brief Flush any pending dirty values immediately
     */
    void flush() {
        if (auto_persist_) {
            auto_persist_->flush();
        }
    }

private:
    /// Auto-persistence for macro values (watches signals, saves on debounce)
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> auto_persist_;
};

}  // namespace core::state
