#include "state/DataManagerWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include "state/CoreState.hpp"

namespace core::state {

FLASHMEM uint8_t DataManagerWorkflow::slotCount(DataManagerCommand command) {
    switch (dataManagerSlotDomain(command)) {
        case DataManagerSlotDomain::MACRO_LIBRARY:
            return persistence::MacroPersistence::LIBRARY_SLOT_COUNT;
        case DataManagerSlotDomain::SEQ_PATTERN_LIBRARY:
            return persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT;
        case DataManagerSlotDomain::SEQ_SET_LIBRARY:
            return persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT;
        case DataManagerSlotDomain::NONE:
        default:
            return 0;
    }
}

FLASHMEM bool DataManagerWorkflow::slotOccupied(CoreState& state,
                                       DataManagerCommand command,
                                       uint8_t slotIndex) {
    using persistence::SlotLoadStatus;

    switch (dataManagerSlotDomain(command)) {
        case DataManagerSlotDomain::MACRO_LIBRARY: {
            if (!state.isMacroPersistenceReady()) return false;

            macro::MacroPagesState probe;
            probe.initDefaults();
            return state.macroPersistence.loadLibrarySlot(slotIndex, probe) == SlotLoadStatus::OK;
        }

        case DataManagerSlotDomain::SEQ_PATTERN_LIBRARY: {
            if (!state.isSequencerPersistenceReady()) return false;

            sequencer::SequencerState probe;
            probe.reset();
            return state.sequencerPersistence.loadPatternSlot(slotIndex, probe) == SlotLoadStatus::OK;
        }

        case DataManagerSlotDomain::SEQ_SET_LIBRARY: {
            if (!state.isSequencerPersistenceReady()) return false;

            sequencer::SequencerTrackBankState probeBank;
            sequencer::SequencerState probe;
            probeBank.reset();
            probe.reset();
            return state.sequencerPersistence.loadSetSlot(slotIndex, probeBank, probe) ==
                   SlotLoadStatus::OK;
        }

        case DataManagerSlotDomain::NONE:
        default:
            return false;
    }
}

FLASHMEM DataManagerCommandExecutionResult DataManagerWorkflow::execute(
    CoreState& state,
    DataManagerCommand command,
    uint8_t slotIndex,
    DataManagerSetLoadMode setLoadMode
) {
    DataManagerCommandExecutionResult result;
    result.handled = true;

    switch (command) {
        case DataManagerCommand::MACRO_SAVE_SLOT:
            result.success = macro::MacroPersistenceWorkflow::saveLibrarySlot(state, slotIndex);
            return result;

        case DataManagerCommand::MACRO_LOAD_SLOT:
            result.isLoadOperation = true;
            result.loadStatus = macro::MacroPersistenceWorkflow::loadLibrarySlot(state, slotIndex);
            result.success = (result.loadStatus == persistence::SlotLoadStatus::OK);
            return result;

        case DataManagerCommand::MACRO_ERASE_SLOT:
            result.success = macro::MacroPersistenceWorkflow::eraseLibrarySlot(state, slotIndex);
            return result;

        case DataManagerCommand::SEQ_SAVE_PATTERN_SLOT:
            result.success = sequencer::SequencerPersistenceWorkflow::savePatternSlot(
                state,
                slotIndex
            );
            return result;

        case DataManagerCommand::SEQ_LOAD_PATTERN_SLOT:
            result.isLoadOperation = true;
            result.loadStatus = sequencer::SequencerPersistenceWorkflow::loadPatternSlot(
                state,
                slotIndex
            );
            result.success = (result.loadStatus == persistence::SlotLoadStatus::OK);
            result.deferredApply = result.success && state.hasPendingSequencerApply();
            return result;

        case DataManagerCommand::SEQ_ERASE_PATTERN_SLOT:
            result.success = sequencer::SequencerPersistenceWorkflow::erasePatternSlot(
                state,
                slotIndex
            );
            return result;

        case DataManagerCommand::SEQ_SAVE_SET_SLOT:
            result.success = sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, slotIndex);
            return result;

        case DataManagerCommand::SEQ_LOAD_SET_SLOT: {
            const bool merge = (setLoadMode == DataManagerSetLoadMode::MERGE);
            result.isLoadOperation = true;
            result.loadStatus = sequencer::SequencerPersistenceWorkflow::loadSetSlot(
                state,
                slotIndex,
                merge
            );
            result.success = (result.loadStatus == persistence::SlotLoadStatus::OK);
            result.deferredApply = result.success && state.hasPendingSequencerApply();
            return result;
        }

        case DataManagerCommand::SEQ_ERASE_SET_SLOT:
            result.success = sequencer::SequencerPersistenceWorkflow::eraseSetSlot(
                state,
                slotIndex
            );
            return result;

        case DataManagerCommand::NONE:
        default:
            result.handled = false;
            result.success = false;
            result.isLoadOperation = false;
            result.loadStatus = persistence::SlotLoadStatus::OUT_OF_RANGE;
            return result;
    }
}

FLASHMEM void DataManagerWorkflow::setShortcut(CoreState& state,
                                     DataManagerContext context,
                                     bool leftButton,
                                     DataManagerCommand command) {
    const DataManagerShortcutSide side =
        leftButton ? DataManagerShortcutSide::LEFT : DataManagerShortcutSide::RIGHT;
    const DataManagerCommand fallback = defaultDataManagerShortcut(context, side);
    const DataManagerCommand sanitized = sanitizeDataManagerShortcut(context, command, fallback);

    if (context == DataManagerContext::MACRO) {
        if (leftButton) {
            state.dataManager.macroShortcutLeft.set(sanitized);
            const auto status =
                state.settings.saveDataManagerMacroShortcutLeftStatus(static_cast<uint8_t>(sanitized));
            if (status != persistence::PersistenceWriteStatus::OK) {
                OC_LOG_WARN("[DataManager] Failed to persist macro left shortcut: {}",
                            persistence::persistenceWriteStatusLabel(status));
            }
        } else {
            state.dataManager.macroShortcutRight.set(sanitized);
            const auto status = state.settings.saveDataManagerMacroShortcutRightStatus(
                static_cast<uint8_t>(sanitized)
            );
            if (status != persistence::PersistenceWriteStatus::OK) {
                OC_LOG_WARN("[DataManager] Failed to persist macro right shortcut: {}",
                            persistence::persistenceWriteStatusLabel(status));
            }
        }
    } else {
        if (leftButton) {
            state.dataManager.seqShortcutLeft.set(sanitized);
            const auto status =
                state.settings.saveDataManagerSeqShortcutLeftStatus(static_cast<uint8_t>(sanitized));
            if (status != persistence::PersistenceWriteStatus::OK) {
                OC_LOG_WARN("[DataManager] Failed to persist sequencer left shortcut: {}",
                            persistence::persistenceWriteStatusLabel(status));
            }
        } else {
            state.dataManager.seqShortcutRight.set(sanitized);
            const auto status = state.settings.saveDataManagerSeqShortcutRightStatus(
                static_cast<uint8_t>(sanitized)
            );
            if (status != persistence::PersistenceWriteStatus::OK) {
                OC_LOG_WARN("[DataManager] Failed to persist sequencer right shortcut: {}",
                            persistence::persistenceWriteStatusLabel(status));
            }
        }
    }

    const auto commitStatus = state.settings.commitStatus();
    if (commitStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[DataManager] Failed to commit shortcut update: {}",
                    persistence::persistenceWriteStatusLabel(commitStatus));
    }
}

FLASHMEM void DataManagerWorkflow::loadShortcutsFromSettings(CoreState& state) {
    uint8_t macroLeft = 0;
    uint8_t macroRight = 0;
    uint8_t seqLeft = 0;
    uint8_t seqRight = 0;
    if (!state.settings.loadDataManagerShortcuts(macroLeft, macroRight, seqLeft, seqRight)) {
        OC_LOG_WARN("[DataManager] Failed to load shortcut settings, using defaults");
    }

    state.dataManager.macroShortcutLeft.set(
        sanitizeDataManagerShortcut(DataManagerContext::MACRO,
                                    static_cast<DataManagerCommand>(macroLeft),
                                    defaultDataManagerShortcut(DataManagerContext::MACRO,
                                                               DataManagerShortcutSide::LEFT))
    );
    state.dataManager.macroShortcutRight.set(
        sanitizeDataManagerShortcut(DataManagerContext::MACRO,
                                    static_cast<DataManagerCommand>(macroRight),
                                    defaultDataManagerShortcut(DataManagerContext::MACRO,
                                                               DataManagerShortcutSide::RIGHT))
    );
    state.dataManager.seqShortcutLeft.set(
        sanitizeDataManagerShortcut(DataManagerContext::SEQUENCER,
                                    static_cast<DataManagerCommand>(seqLeft),
                                    defaultDataManagerShortcut(DataManagerContext::SEQUENCER,
                                                               DataManagerShortcutSide::LEFT))
    );
    state.dataManager.seqShortcutRight.set(
        sanitizeDataManagerShortcut(DataManagerContext::SEQUENCER,
                                    static_cast<DataManagerCommand>(seqRight),
                                    defaultDataManagerShortcut(DataManagerContext::SEQUENCER,
                                                               DataManagerShortcutSide::RIGHT))
    );
}

}  // namespace core::state
