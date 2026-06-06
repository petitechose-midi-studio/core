#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

#include "DataManagerCatalog.hpp"

namespace core::state {

/**
 * Session state for the Data Manager overlay.
 *
 * It stores UI flow, shortcuts, pending command intent, and feedback only.
 * Actual persistence operations are performed by DataManagerWorkflow.
 */
enum class DataManagerFlowPhase : uint8_t {
    CLOSED = 0,
    MANAGER = 1,
    ASSIGN_SHORTCUT = 2,
    COMMAND_PALETTE = 3,
    SLOT_PICKER = 4,
    SET_LOAD_MODE = 5,
    CONFIRM = 6,
};

inline constexpr DataManagerFlowPhase dataManagerFlowPhaseForDialogMode(DataManagerDialogMode mode) {
    switch (mode) {
        case DataManagerDialogMode::ASSIGN_SHORTCUT:
            return DataManagerFlowPhase::ASSIGN_SHORTCUT;
        case DataManagerDialogMode::COMMAND_PALETTE:
            return DataManagerFlowPhase::COMMAND_PALETTE;
        case DataManagerDialogMode::SLOT_PICKER:
            return DataManagerFlowPhase::SLOT_PICKER;
        case DataManagerDialogMode::SET_LOAD_MODE:
            return DataManagerFlowPhase::SET_LOAD_MODE;
        case DataManagerDialogMode::CONFIRM:
            return DataManagerFlowPhase::CONFIRM;
        default:
            return DataManagerFlowPhase::MANAGER;
    }
}

inline constexpr bool dataManagerFlowShowsDialog(DataManagerFlowPhase phase) {
    switch (phase) {
        case DataManagerFlowPhase::ASSIGN_SHORTCUT:
        case DataManagerFlowPhase::COMMAND_PALETTE:
        case DataManagerFlowPhase::SLOT_PICKER:
        case DataManagerFlowPhase::SET_LOAD_MODE:
        case DataManagerFlowPhase::CONFIRM:
            return true;
        case DataManagerFlowPhase::CLOSED:
        case DataManagerFlowPhase::MANAGER:
        default:
            return false;
    }
}

struct DataManagerDialogState {
    oc::state::Signal<bool, 4> visible{false};
    oc::state::Signal<DataManagerDialogMode, 4> mode{
        DataManagerDialogMode::ASSIGN_SHORTCUT
    };
    oc::state::Signal<int, 4> selectedIndex{0};
    oc::state::Signal<uint8_t, 4> editingShortcutRow{0};

    void reset();
};

struct DataManagerState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<DataManagerContext, 4> context{DataManagerContext::MACRO};
    oc::state::Signal<DataManagerFlowPhase, 4> flowPhase{DataManagerFlowPhase::CLOSED};

    oc::state::Signal<DataManagerCommand> macroShortcutLeft{DEFAULT_MACRO_SHORTCUT_LEFT};
    oc::state::Signal<DataManagerCommand> macroShortcutRight{DEFAULT_MACRO_SHORTCUT_RIGHT};
    oc::state::Signal<DataManagerCommand> seqShortcutLeft{DEFAULT_SEQ_SHORTCUT_LEFT};
    oc::state::Signal<DataManagerCommand> seqShortcutRight{DEFAULT_SEQ_SHORTCUT_RIGHT};

    oc::state::Signal<DataManagerCommand> pendingCommand{DataManagerCommand::NONE};
    oc::state::Signal<uint8_t> pendingSlot{0};
    oc::state::Signal<DataManagerSetLoadMode> pendingSetLoadMode{DataManagerSetLoadMode::REPLACE};

    oc::state::SignalLabel feedback;
    DataManagerDialogState dialog;

    DataManagerState();
    ~DataManagerState();

    void resetSession(DataManagerContext activeContext);
    void openSession(DataManagerContext activeContext);
    void closeSession();
    void showDialog(DataManagerDialogMode mode,
                    int selectedIndex,
                    uint8_t editingShortcutRow = 0);
    void closeDialog();
    void clearPendingCommand();
    DataManagerCommand shortcutForSide(DataManagerShortcutSide side) const;
    DataManagerCommand shortcutForRow(uint8_t row) const;

    uint8_t rowCount() const {
        return 2U;
    }
};

}  // namespace core::state
