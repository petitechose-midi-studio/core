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
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

#include <oc/interface/IStorage.hpp>
#include <oc/state/AutoPersistIncremental.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "persistence/MacroPersistence.hpp"
#include "persistence/SequencerPersistence.hpp"
#include "CoreSettings.hpp"
#include "DataManagerState.hpp"
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
        // Load persisted settings
        settings.load(pages, midiSync);
        loadDataManagerShortcutsFromSettings_();

        macro_persistence_ready_ = macroPersistence.init();
        if (macro_persistence_ready_) {
            if (!macroPersistence.loadWorkspace(pages)) {
                persistMacroWorkspace_();
            }
        }

        sequencer_persistence_ready_ = sequencerPersistence.init();
        if (sequencer_persistence_ready_) {
            if (!sequencerPersistence.loadWorkspace(sequencer)) {
                persistSequencerWorkspace_();
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
        overlays.registerItem(core::ui::OverlayType::DATA_MANAGER, dataManager.visible);
        overlays.registerItem(core::ui::OverlayType::DATA_MANAGER_DIALOG,
                              dataManager.dialog.visible);

        // Setup auto-persistence for macro values
        macro_auto_persist_ = std::make_unique<oc::state::AutoPersistIncremental<MACRO_COUNT>>(
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
            macro_auto_persist_->watchAt(i, macros.slots[i].value);
        }

        sequencer_auto_persist_ = std::make_unique<oc::state::AutoPersistIncremental<5>>(
            [](uint8_t) {},
            [this]() { persistSequencerWorkspace_(); },
            CoreSettings::VALUE_SAVE_DELAY_MS
        );

        sequencer_auto_persist_->watchAt(0, sequencer.length);
        sequencer_auto_persist_->watchAt(1, sequencer.stepsPerBeat);
        sequencer_auto_persist_->watchAt(2, sequencer.midiChannel);
        sequencer_auto_persist_->watchAt(3, sequencer.enabledMask);
        sequencer_auto_persist_->watchAt(4, sequencer.stepDataRevision);
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

        // Explicit library saves should snapshot current runtime macro values,
        // even if AutoPersist debounce has not propagated them into page storage yet.
        syncActivePageValuesFromRuntime_();

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

    bool saveSequencerPatternSlot(uint8_t slotIndex) {
        if (!sequencer_persistence_ready_) return false;
        return sequencerPersistence.savePatternSlot(slotIndex, sequencer);
    }

    persistence::SlotLoadStatus loadSequencerPatternSlot(uint8_t slotIndex) {
        if (!sequencer_persistence_ready_) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;

        if (statusBar.playing.get()) {
            sequencer::SequencerState staged;
            const persistence::SlotLoadStatus status =
                sequencerPersistence.loadPatternSlot(slotIndex, staged);
            if (status == persistence::SlotLoadStatus::OK) {
                queueSequencerApply_(staged);
            }
            return status;
        }

        const persistence::SlotLoadStatus status = sequencerPersistence.loadPatternSlot(slotIndex, sequencer);
        if (status == persistence::SlotLoadStatus::OK) {
            persistSequencerWorkspace_();
        }

        return status;
    }

    bool eraseSequencerPatternSlot(uint8_t slotIndex) {
        if (!sequencer_persistence_ready_) return false;
        return sequencerPersistence.erasePatternSlot(slotIndex);
    }

    bool saveSequencerSetSlot(uint8_t slotIndex) {
        if (!sequencer_persistence_ready_) return false;
        return sequencerPersistence.saveSetSlot(slotIndex, sequencer);
    }

    persistence::SlotLoadStatus loadSequencerSetSlot(uint8_t slotIndex,
                                                     bool merge = false) {
        if (!sequencer_persistence_ready_) return persistence::SlotLoadStatus::STORAGE_UNAVAILABLE;

        if (statusBar.playing.get()) {
            sequencer::SequencerState staged;
            const persistence::SlotLoadStatus status =
                sequencerPersistence.loadSetSlot(slotIndex, staged);
            if (status == persistence::SlotLoadStatus::OK) {
                queueSequencerApply_(staged, merge);
            }
            return status;
        }

        sequencer::SequencerState staged;
        const persistence::SlotLoadStatus status = sequencerPersistence.loadSetSlot(slotIndex, staged);
        if (status == persistence::SlotLoadStatus::OK) {
            if (merge) {
                mergeSequencerSnapshotIntoCurrent_(staged);
            } else {
                applySequencerSnapshotFromState_(staged);
            }
            persistSequencerWorkspace_();
        }

        return status;
    }

    bool eraseSequencerSetSlot(uint8_t slotIndex) {
        if (!sequencer_persistence_ready_) return false;
        return sequencerPersistence.eraseSetSlot(slotIndex);
    }

    DataManagerCommand dataManagerShortcut(DataManagerContext context,
                                           bool leftButton) const {
        if (context == DataManagerContext::MACRO) {
            return leftButton ? dataManager.macroShortcutLeft.get()
                              : dataManager.macroShortcutRight.get();
        }

        return leftButton ? dataManager.seqShortcutLeft.get()
                          : dataManager.seqShortcutRight.get();
    }

    void setDataManagerShortcut(DataManagerContext context,
                                bool leftButton,
                                DataManagerCommand command) {
        const DataManagerCommand fallback = defaultDataManagerShortcut_(context, leftButton);
        const DataManagerCommand sanitized = sanitizeDataManagerShortcut(context, command, fallback);

        if (context == DataManagerContext::MACRO) {
            if (leftButton) {
                dataManager.macroShortcutLeft.set(sanitized);
                settings.saveDataManagerMacroShortcutLeft(static_cast<uint8_t>(sanitized));
            } else {
                dataManager.macroShortcutRight.set(sanitized);
                settings.saveDataManagerMacroShortcutRight(static_cast<uint8_t>(sanitized));
            }
        } else {
            if (leftButton) {
                dataManager.seqShortcutLeft.set(sanitized);
                settings.saveDataManagerSeqShortcutLeft(static_cast<uint8_t>(sanitized));
            } else {
                dataManager.seqShortcutRight.set(sanitized);
                settings.saveDataManagerSeqShortcutRight(static_cast<uint8_t>(sanitized));
            }
        }

        settings.commit();
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
        applyPendingSequencerApplyIfReady_();

        if (macro_auto_persist_) {
            macro_auto_persist_->update();
        }
        if (sequencer_auto_persist_) {
            sequencer_auto_persist_->update();
        }
    }

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset() {
        settings.factoryReset();
        pages.initDefaults();
        midiSync.reset();
        syncMacrosFromActivePage();
        settings.saveAll(pages, midiSync);
        loadDataManagerShortcutsFromSettings_();
        persistMacroWorkspace_();
        statusBar.pageName.set(pages.activePageData().name);
        macroEdit.reset();
        viewSelector.reset();
        sequencer.reset();
        pending_sequencer_apply_.valid = false;
        persistSequencerWorkspace_();
        globalSettings.reset();
        dataManager.resetSession(DataManagerContext::MACRO);
        dataManager.feedback.set("");
        activeView.set(core::ui::ViewType::MACRO);
        overlays.hideAll();
        configRevision.set(configRevision.get() + 1);
    }

    /**
     * @brief Flush any pending dirty values immediately
     */
    void flush() {
        if (macro_auto_persist_) {
            macro_auto_persist_->flush();
        }
        if (sequencer_auto_persist_) {
            sequencer_auto_persist_->flush();
        }
    }

private:
    static DataManagerCommand defaultDataManagerShortcut_(DataManagerContext context,
                                                          bool leftButton) {
        if (context == DataManagerContext::MACRO) {
            return leftButton ? DEFAULT_MACRO_SHORTCUT_LEFT : DEFAULT_MACRO_SHORTCUT_RIGHT;
        }
        return leftButton ? DEFAULT_SEQ_SHORTCUT_LEFT : DEFAULT_SEQ_SHORTCUT_RIGHT;
    }

    void loadDataManagerShortcutsFromSettings_() {
        uint8_t macroLeft = 0;
        uint8_t macroRight = 0;
        uint8_t seqLeft = 0;
        uint8_t seqRight = 0;
        settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight);

        dataManager.macroShortcutLeft.set(
            sanitizeDataManagerShortcut(DataManagerContext::MACRO,
                                        static_cast<DataManagerCommand>(macroLeft),
                                        DEFAULT_MACRO_SHORTCUT_LEFT)
        );
        dataManager.macroShortcutRight.set(
            sanitizeDataManagerShortcut(DataManagerContext::MACRO,
                                        static_cast<DataManagerCommand>(macroRight),
                                        DEFAULT_MACRO_SHORTCUT_RIGHT)
        );
        dataManager.seqShortcutLeft.set(
            sanitizeDataManagerShortcut(DataManagerContext::SEQUENCER,
                                        static_cast<DataManagerCommand>(seqLeft),
                                        DEFAULT_SEQ_SHORTCUT_LEFT)
        );
        dataManager.seqShortcutRight.set(
            sanitizeDataManagerShortcut(DataManagerContext::SEQUENCER,
                                        static_cast<DataManagerCommand>(seqRight),
                                        DEFAULT_SEQ_SHORTCUT_RIGHT)
        );
    }

    struct SequencerPatternSnapshot {
        uint8_t length = oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
        uint8_t stepsPerBeat = oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
        uint8_t midiChannel = oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED;
        uint64_t enabledMask = 0;
        std::array<uint8_t, sequencer::SequencerState::MAX_STEPS> note{};
        std::array<uint8_t, sequencer::SequencerState::MAX_STEPS> velocity{};
        std::array<uint16_t, sequencer::SequencerState::MAX_STEPS> gate{};
        std::array<int8_t, sequencer::SequencerState::MAX_STEPS> nudge{};
    };

    struct PendingSequencerApply {
        bool valid = false;
        int16_t anchorPlayhead = -1;
        bool merge = false;
        SequencerPatternSnapshot snapshot{};
    };

    static uint64_t lengthMask_(uint8_t length) {
        if (length == 0) return 0;
        if (length >= sequencer::SequencerState::MAX_STEPS) return ~uint64_t{0};
        return (uint64_t{1} << length) - uint64_t{1};
    }

    static uint8_t sanitizeSequencerLength_(uint8_t length) {
        if (length == 0 || length > sequencer::SequencerState::MAX_STEPS) {
            return oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
        }
        return length;
    }

    static uint8_t sanitizeStepsPerBeat_(uint8_t spb) {
        if (spb == 0) {
            return oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
        }
        return spb;
    }

    static uint8_t sanitizeMidiChannel_(uint8_t channel) {
        return (channel > 15U)
                   ? oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED
                   : channel;
    }

    static uint8_t sanitizeMidi7_(uint8_t value) {
        return (value > 127U) ? 127U : value;
    }

    static void captureSequencerSnapshot_(const sequencer::SequencerState& source,
                                          SequencerPatternSnapshot& out) {
        out.length = sanitizeSequencerLength_(source.length.get());
        out.stepsPerBeat = sanitizeStepsPerBeat_(source.stepsPerBeat.get());
        out.midiChannel = sanitizeMidiChannel_(source.midiChannel.get());
        out.enabledMask = source.enabledMask.get();

        for (uint8_t i = 0; i < sequencer::SequencerState::MAX_STEPS; ++i) {
            out.note[i] = sanitizeMidi7_(source.note[i]);
            out.velocity[i] = sanitizeMidi7_(source.velocity[i]);
            out.gate[i] = sequencer::SequencerState::clampGatePercent(source.gate[i]);
            out.nudge[i] = source.nudge[i];
        }
    }

    void applySequencerSnapshot_(const SequencerPatternSnapshot& snapshot) {
        const uint8_t length = sanitizeSequencerLength_(snapshot.length);
        const uint8_t focused_before = sequencer.focusedStep.get();

        sequencer.length.set(length);
        sequencer.stepsPerBeat.set(sanitizeStepsPerBeat_(snapshot.stepsPerBeat));
        sequencer.midiChannel.set(sanitizeMidiChannel_(snapshot.midiChannel));
        sequencer.enabledMask.set(snapshot.enabledMask & lengthMask_(length));

        for (uint8_t i = 0; i < sequencer::SequencerState::MAX_STEPS; ++i) {
            sequencer.note[i] = sanitizeMidi7_(snapshot.note[i]);
            sequencer.velocity[i] = sanitizeMidi7_(snapshot.velocity[i]);
            sequencer.gate[i] = sequencer::SequencerState::clampGatePercent(snapshot.gate[i]);
            sequencer.nudge[i] = snapshot.nudge[i];
        }

        const uint8_t focused =
            (focused_before >= length) ? static_cast<uint8_t>(length - 1U) : focused_before;
        sequencer.focusedStep.set(focused);
        sequencer.page.set(sequencer.pageForStep(focused));
        sequencer.bumpStepDataRevision();
    }

    void applySequencerSnapshotFromState_(const sequencer::SequencerState& source) {
        SequencerPatternSnapshot snapshot;
        captureSequencerSnapshot_(source, snapshot);
        applySequencerSnapshot_(snapshot);
    }

    void mergeSequencerSnapshotIntoCurrent_(const sequencer::SequencerState& incoming) {
        SequencerPatternSnapshot snapshot;
        captureSequencerSnapshot_(incoming, snapshot);
        mergeSequencerSnapshotIntoCurrent_(snapshot);
    }

    void mergeSequencerSnapshotIntoCurrent_(const SequencerPatternSnapshot& snapshot) {
        const uint8_t focused_before = sequencer.focusedStep.get();

        const uint8_t currentLength = sanitizeSequencerLength_(sequencer.length.get());
        const uint8_t incomingLength = sanitizeSequencerLength_(snapshot.length);
        const uint8_t mergedLength = std::max(currentLength, incomingLength);

        sequencer.length.set(mergedLength);

        uint64_t mergedMask = sequencer.enabledMask.get() & lengthMask_(mergedLength);
        const uint64_t incomingMask = snapshot.enabledMask & lengthMask_(incomingLength);

        for (uint8_t i = 0; i < incomingLength; ++i) {
            const uint64_t bit = uint64_t{1} << i;
            if ((incomingMask & bit) == 0) continue;

            sequencer.note[i] = sanitizeMidi7_(snapshot.note[i]);
            sequencer.velocity[i] = sanitizeMidi7_(snapshot.velocity[i]);
            sequencer.gate[i] = sequencer::SequencerState::clampGatePercent(snapshot.gate[i]);
            sequencer.nudge[i] = snapshot.nudge[i];
            mergedMask |= bit;
        }

        sequencer.enabledMask.set(mergedMask);

        const uint8_t focused =
            (focused_before >= mergedLength) ? static_cast<uint8_t>(mergedLength - 1U) : focused_before;
        sequencer.focusedStep.set(focused);
        sequencer.page.set(sequencer.pageForStep(focused));
        sequencer.bumpStepDataRevision();
    }

    void queueSequencerApply_(const sequencer::SequencerState& staged,
                              bool merge = false) {
        captureSequencerSnapshot_(staged, pending_sequencer_apply_.snapshot);
        pending_sequencer_apply_.anchorPlayhead = sequencer.playheadStep.get();
        pending_sequencer_apply_.merge = merge;
        pending_sequencer_apply_.valid = true;
    }

    void applyPendingSequencerApplyIfReady_() {
        if (!pending_sequencer_apply_.valid) return;

        if (statusBar.playing.get()) {
            const int16_t playhead = sequencer.playheadStep.get();
            if (playhead < 0) return;
            if (playhead == pending_sequencer_apply_.anchorPlayhead) return;
        }

        if (pending_sequencer_apply_.merge) {
            mergeSequencerSnapshotIntoCurrent_(pending_sequencer_apply_.snapshot);
        } else {
            applySequencerSnapshot_(pending_sequencer_apply_.snapshot);
        }
        pending_sequencer_apply_.valid = false;
        persistSequencerWorkspace_();
    }

    void persistMacroWorkspace_() {
        if (!macro_persistence_ready_) return;
        macroPersistence.saveWorkspace(pages);
    }

    void syncActivePageValuesFromRuntime_() {
        auto& page = pages.activePageData();
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            page.values[i] = std::clamp(macros.slots[i].value.get(), 0.0f, 1.0f);
        }
    }

    void persistSequencerWorkspace_() {
        if (!sequencer_persistence_ready_) return;
        sequencerPersistence.saveWorkspace(sequencer);
    }

    bool macro_persistence_ready_ = false;
    bool sequencer_persistence_ready_ = false;
    PendingSequencerApply pending_sequencer_apply_{};

    /// Auto-persistence for macro values (watches signals, saves on debounce)
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> macro_auto_persist_;

    /// Auto-persistence for sequencer workspace (watches key sequencer signals)
    std::unique_ptr<oc::state::AutoPersistIncremental<5>> sequencer_auto_persist_;
};

}  // namespace core::state
