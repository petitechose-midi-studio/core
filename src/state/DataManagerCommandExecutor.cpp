#include "state/DataManagerCommandExecutor.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"
#include "state/macro/MacroPersistenceWorkflow.hpp"
#include "state/sequencer/SequencerPersistenceWorkflow.hpp"

namespace core::state::data_manager {

FLASHMEM uint8_t slotCount(DataManagerCommand command) {
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

FLASHMEM bool slotOccupied(CoreState& state, DataManagerCommand command, uint8_t slotIndex) {
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
            return state.sequencerPersistence.loadPatternSlot(slotIndex, probe) ==
                   SlotLoadStatus::OK;
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

FLASHMEM DataManagerCommandExecutionResult execute(CoreState& state,
                                                   DataManagerCommand command,
                                                   uint8_t slotIndex,
                                                   DataManagerSetLoadMode setLoadMode) {
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

}  // namespace core::state::data_manager
