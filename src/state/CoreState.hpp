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
#include <memory>

#include <oc/interface/IStorage.hpp>
#include <oc/state/AutoPersistIncremental.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "CoreSettings.hpp"
#include "DataManagerState.hpp"
#include "GlobalSettingsState.hpp"
#include "MidiSyncState.hpp"
#include "MacroEditState.hpp"
#include "MacroState.hpp"
#include "../ui/OverlayTypes.hpp"
#include "../ui/ViewTypes.hpp"
#include "StatusBarState.hpp"
#include "ViewSelectorState.hpp"
#include "persistence/MacroPersistence.hpp"
#include "persistence/SequencerPersistence.hpp"
#include "macro/MacroPagesState.hpp"
#include "sequencer/SequencerState.hpp"
#include "sequencer/SequencerSnapshots.hpp"
#include "sequencer/SequencerTrackBankState.hpp"

namespace core::state {

struct CoreStateBootstrap;
struct CoreStateLifecycle;

struct MacroDomainState {
    MacroState runtime;
    macro::MacroPagesState pages;
    oc::state::Signal<uint32_t> configRevision{0};
    persistence::MacroPersistence persistence;
    bool persistenceReady = false;
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> autoPersist;

    MacroDomainState(oc::interface::IStorage& workspaceStorage,
                     oc::interface::IStorage& libraryStorage)
        : persistence(workspaceStorage, libraryStorage) {}

    MacroDomainState(const MacroDomainState&) = delete;
    MacroDomainState& operator=(const MacroDomainState&) = delete;
};

struct SequencerDomainState {
    struct PendingApply {
        bool valid = false;
        int16_t anchorPlayhead = -1;
        bool merge = false;
        bool fullBank = false;
        sequencer::SequencerPatternSnapshot snapshot{};
        sequencer::SequencerTrackBankSnapshot bankSnapshot{};
    };

    sequencer::SequencerState editor;
    sequencer::SequencerTrackBankState tracks;
    persistence::SequencerPersistence persistence;
    bool persistenceReady = false;
    PendingApply pendingApply{};
    std::unique_ptr<oc::state::AutoPersistIncremental<8>> autoPersist;

    SequencerDomainState(oc::interface::IStorage& workspaceStorage,
                         oc::interface::IStorage& patternLibraryStorage,
                         oc::interface::IStorage& setLibraryStorage)
        : persistence(workspaceStorage, patternLibraryStorage, setLibraryStorage) {}

    SequencerDomainState(const SequencerDomainState&) = delete;
    SequencerDomainState& operator=(const SequencerDomainState&) = delete;
};

struct UiSystemState {
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType> overlays;
    oc::state::Signal<core::ui::ViewType> activeView{core::ui::ViewType::MACRO};
    ViewSelectorState viewSelector;
    StatusBarState statusBar;
    MidiSyncState midiSync;
    GlobalSettingsState globalSettings;
    DataManagerState dataManager;
    MacroEditState macroEdit;
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

private:
    MacroDomainState macroDomain_;
    SequencerDomainState sequencerDomain_;
    UiSystemState systemUi_;

public:
    /// Persistence manager
    CoreSettings settings;

    /// Macro domain aliases
    MacroState& macros;
    macro::MacroPagesState& pages;
    oc::state::Signal<uint32_t>& configRevision;
    persistence::MacroPersistence& macroPersistence;

    /// Sequencer domain aliases
    sequencer::SequencerState& sequencer;
    sequencer::SequencerTrackBankState& sequencerTracks;
    persistence::SequencerPersistence& sequencerPersistence;

    /// Shared UI/system domain aliases
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
    oc::state::Signal<core::ui::ViewType>& activeView;
    ViewSelectorState& viewSelector;
    StatusBarState& statusBar;
    MidiSyncState& midiSync;
    GlobalSettingsState& globalSettings;
    DataManagerState& dataManager;
    MacroEditState& macroEdit;

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
                       oc::interface::IStorage& sequencerSetLibraryStorage);

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
    void update();

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset();

    /**
     * @brief Flush any pending dirty values immediately
     */
    void flush();
    void resetStandaloneTransientUi();

    bool isMacroPersistenceReady() const;
    bool isSequencerPersistenceReady() const;
    void persistMacroWorkspace();
    void persistSequencerWorkspace();
    void queuePendingSequencerApply(const sequencer::SequencerState& staged, bool merge = false);
    void queuePendingSequencerBankApply(const sequencer::SequencerTrackBankSnapshot& staged);
    void clearPendingSequencerApply();
    bool hasPendingSequencerApply() const;

private:
    void queueSequencerApply_(const sequencer::SequencerState& staged, bool merge = false);
    void queueSequencerBankApply_(const sequencer::SequencerTrackBankSnapshot& staged);
    void persistMacroWorkspace_();
    void persistSequencerWorkspace_();
    void clearPendingSequencerApply_();

};

}  // namespace core::state
