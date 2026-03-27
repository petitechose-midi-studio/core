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

#include <cstdint>
#include <cstdio>
#include <memory>

#include <oc/interface/IStorage.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/AutoPersistIncremental.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "persistence/MacroPersistence.hpp"
#include "persistence/SequencerPersistence.hpp"
#include "CoreSettings.hpp"
#include "CoreStateBootstrap.hpp"
#include "CoreStateLifecycle.hpp"
#include "DataManagerState.hpp"
#include "DataManagerWorkflow.hpp"
#include "GlobalSettingsState.hpp"
#include "MidiSyncState.hpp"
#include "MacroEditState.hpp"
#include "MacroState.hpp"
#include "../ui/OverlayTypes.hpp"
#include "../ui/ViewTypes.hpp"
#include "StatusBarState.hpp"
#include "macro/MacroPagesState.hpp"
#include "macro/MacroPersistenceWorkflow.hpp"
#include "macro/MacroWorkflow.hpp"
#include "sequencer/SequencerPersistenceWorkflow.hpp"
#include "sequencer/SequencerSnapshotOps.hpp"
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
    friend struct CoreStateBootstrap;
    friend struct CoreStateLifecycle;

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

    /// Sequencer workspace + pattern/set libraries persistence service
    persistence::SequencerPersistence sequencerPersistence;

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

    /// Data manager overlays state
    DataManagerState dataManager;

    /// Macro edit overlay state
    MacroEditState macroEdit;

    /**
     * @brief Construct with storage backend
     * @param settingsStorage Legacy settings storage (midi sync + migration source)
     * @param macroWorkspaceStorage Dedicated macro workspace storage
     * @param macroLibraryStorage Dedicated macro library storage
     * @param sequencerWorkspaceStorage Dedicated sequencer workspace storage
     * @param sequencerPatternLibraryStorage Dedicated sequencer pattern library storage
     * @param sequencerSetLibraryStorage Dedicated sequencer set library storage
     */
    explicit CoreState(oc::interface::IStorage& settingsStorage,
                       oc::interface::IStorage& macroWorkspaceStorage,
                       oc::interface::IStorage& macroLibraryStorage,
                       oc::interface::IStorage& sequencerWorkspaceStorage,
                       oc::interface::IStorage& sequencerPatternLibraryStorage,
                       oc::interface::IStorage& sequencerSetLibraryStorage)
        : settings(settingsStorage)
        , macroPersistence(macroWorkspaceStorage, macroLibraryStorage)
        , sequencerPersistence(sequencerWorkspaceStorage,
                               sequencerPatternLibraryStorage,
                               sequencerSetLibraryStorage) {
        CoreStateBootstrap::initialize(*this);
    }

    // Non-copyable, non-movable
    CoreState(const CoreState&) = delete;
    CoreState& operator=(const CoreState&) = delete;
    CoreState(CoreState&&) = delete;
    CoreState& operator=(CoreState&&) = delete;

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Update persistence (call from main loop)
     *
     * Saves dirty values incrementally after debounce timeout.
     */
    void update() {
        CoreStateLifecycle::update(*this);
    }

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset() {
        CoreStateLifecycle::factoryReset(*this);
    }

    /**
     * @brief Flush any pending dirty values immediately
     */
    void flush() {
        CoreStateLifecycle::flush(*this);
    }

    bool isMacroPersistenceReady() const { return macro_persistence_ready_; }
    bool isSequencerPersistenceReady() const { return sequencer_persistence_ready_; }
    void persistMacroWorkspace() { persistMacroWorkspace_(); }
    void persistSequencerWorkspace() { persistSequencerWorkspace_(); }
    void queuePendingSequencerApply(const sequencer::SequencerState& staged, bool merge = false) {
        queueSequencerApply_(staged, merge);
    }
    void clearPendingSequencerApply() { clearPendingSequencerApply_(); }
    bool hasPendingSequencerApply() const { return pending_sequencer_apply_.valid; }

private:
    struct PendingSequencerApply {
        bool valid = false;
        int16_t anchorPlayhead = -1;
        bool merge = false;
        sequencer::SequencerPatternSnapshot snapshot{};
    };

    void queueSequencerApply_(const sequencer::SequencerState& staged,
                              bool merge = false) {
        CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
    }

    void persistMacroWorkspace_() {
        if (!macro_persistence_ready_) return;
        const auto status = macroPersistence.saveWorkspaceStatus(pages);
        if (status != persistence::PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[CoreState] Failed to persist macro workspace: {}",
                        persistence::persistenceWriteStatusLabel(status));
        }
    }

    void persistSequencerWorkspace_() {
        if (!sequencer_persistence_ready_) return;
        const auto status = sequencerPersistence.saveWorkspaceStatus(sequencer);
        if (status != persistence::PersistenceWriteStatus::OK) {
            OC_LOG_WARN("[CoreState] Failed to persist sequencer workspace: {}",
                        persistence::persistenceWriteStatusLabel(status));
        }
    }

    void clearPendingSequencerApply_() {
        CoreStateLifecycle::clearPendingSequencerApply(*this);
    }

    bool macro_persistence_ready_ = false;
    bool sequencer_persistence_ready_ = false;
    PendingSequencerApply pending_sequencer_apply_{};

    /// Auto-persistence for macro values (watches signals, saves on debounce)
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> macro_auto_persist_;

    /// Auto-persistence for sequencer workspace (watches key sequencer signals)
    std::unique_ptr<oc::state::AutoPersistIncremental<8>> sequencer_auto_persist_;
};

}  // namespace core::state
