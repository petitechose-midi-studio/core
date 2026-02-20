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

#include "persistence/MacroPersistence.hpp"
#include "CoreSettings.hpp"
#include "GlobalSettingsState.hpp"
#include "MidiSyncState.hpp"
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

    /// Macro workspace + library persistence service
    persistence::MacroPersistence macroPersistence;

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

    /// MIDI sync mode/source state
    MidiSyncState midiSync;

    /// Global settings overlays state
    GlobalSettingsState globalSettings;

    /// Macro edit overlay state
    MacroEditState macroEdit;

    /**
     * @brief Construct with storage backend
     * @param settingsStorage Legacy settings storage (midi sync + migration source)
     * @param macroWorkspaceStorage Dedicated macro workspace storage
     * @param macroLibraryStorage Dedicated macro library storage
     */
    explicit CoreState(oc::interface::IStorage& settingsStorage,
                       oc::interface::IStorage& macroWorkspaceStorage,
                       oc::interface::IStorage& macroLibraryStorage)
        : settings(settingsStorage)
        , macroPersistence(macroWorkspaceStorage, macroLibraryStorage) {
        // Load persisted settings
        settings.load(pages, midiSync);

        macro_persistence_ready_ = macroPersistence.init();
        if (macro_persistence_ready_) {
            if (!macroPersistence.loadWorkspace(pages)) {
                persistMacroWorkspace_();
            }
        }

        // Reflect loaded page name in UI state
        statusBar.pageName.set(pages.activePageData().name);

        // Sync runtime macros with active page
        syncMacrosFromActivePage();

        // Register overlay signals
        overlays.registerItem(core::ui::OverlayType::PAGE_SELECTOR, pages.selector.visible);
        overlays.registerItem(core::ui::OverlayType::MACRO_EDIT, macroEdit.visible);
        overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_SELECTOR, macroEdit.selector.visible);
        overlays.registerItem(core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR, macroEdit.macroSelector.visible);
        overlays.registerItem(core::ui::OverlayType::VIEW_SELECTOR, viewSelector.visible);

        overlays.registerItem(core::ui::OverlayType::SEQ_PATTERN_CONFIG, sequencer.patternConfig.visible);
        overlays.registerItem(core::ui::OverlayType::SEQ_STEP_EDIT, sequencer.stepEdit.visible);
        overlays.registerItem(core::ui::OverlayType::SEQ_PROPERTY_SELECTOR, sequencer.propertySelector.visible);
        overlays.registerItem(core::ui::OverlayType::GLOBAL_SETTINGS, globalSettings.visible);
        overlays.registerItem(core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR, globalSettings.selector.visible);

        // Setup auto-persistence for macro values
        auto_persist_ = std::make_unique<oc::state::AutoPersistIncremental<MACRO_COUNT>>(
            [this](uint8_t i) {
                float value = macros.slots[i].value.get();

                // Avoid redundant writes (e.g., when loading values from storage/page sync).
                auto& page = pages.activePageData();
                if (page.values[i] == value) return;
                page.values[i] = value;
            },
            [this]() { persistMacroWorkspace_(); },
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
        persistMacroWorkspace_();

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
     * Single source of truth: updates page data, derived active configs, and staged persistence.
     * @return true when channel or CC changed
     */
    bool setMacroConfig(uint8_t index, uint8_t channel, uint8_t cc) {
        if (index >= MACRO_COUNT) return false;
        if (channel > 15 || cc > 127) return false;

        auto& page = pages.activePageData();
        const bool channelChanged = page.channel[index] != channel;
        const bool ccChanged = page.cc[index] != cc;
        if (!channelChanged && !ccChanged) {
            return false;
        }

        page.channel[index] = channel;
        page.cc[index] = cc;
        pages.updateActiveConfigs();

        persistMacroWorkspace_();

        return true;
    }

    bool saveMacroLibrarySlot(uint8_t slotIndex) {
        if (!macro_persistence_ready_) return false;
        return macroPersistence.saveLibrarySlot(slotIndex, pages);
    }

    persistence::SlotLoadStatus loadMacroLibrarySlot(uint8_t slotIndex) {
        if (!macro_persistence_ready_) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;

        const persistence::SlotLoadStatus status = macroPersistence.loadLibrarySlot(slotIndex, pages);
        if (status == persistence::SlotLoadStatus::OK) {
            statusBar.pageName.set(pages.activePageData().name);
            syncMacrosFromActivePage();
            persistMacroWorkspace_();
            configRevision.set(configRevision.get() + 1);
        }

        return status;
    }

    bool eraseMacroLibrarySlot(uint8_t slotIndex) {
        if (!macro_persistence_ready_) return false;
        return macroPersistence.eraseLibrarySlot(slotIndex);
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
        settings.saveAll(pages, midiSync);
        persistMacroWorkspace_();
        statusBar.pageName.set(pages.activePageData().name);
        macroEdit.reset();
        viewSelector.reset();
        sequencer.reset();
        midiSync.reset();
        globalSettings.reset();
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
    void persistMacroWorkspace_() {
        if (!macro_persistence_ready_) return;
        macroPersistence.saveWorkspace(pages);
    }

    bool macro_persistence_ready_ = false;

    /// Auto-persistence for macro values (watches signals, saves on debounce)
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> auto_persist_;
};

}  // namespace core::state
