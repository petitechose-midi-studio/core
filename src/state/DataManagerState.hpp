#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

namespace core::state {

enum class DataManagerContext : uint8_t {
    MACRO = 0,
    SEQUENCER = 1,
};

enum class DataManagerCommand : uint8_t {
    NONE = 0,

    // Macro context commands
    MACRO_SAVE_SLOT = 1,
    MACRO_LOAD_SLOT = 2,
    MACRO_ERASE_SLOT = 3,

    // Sequencer context commands
    SEQ_SAVE_PATTERN_SLOT = 4,
    SEQ_LOAD_PATTERN_SLOT = 5,
    SEQ_ERASE_PATTERN_SLOT = 6,
    SEQ_SAVE_SET_SLOT = 7,
    SEQ_LOAD_SET_SLOT = 8,
    SEQ_ERASE_SET_SLOT = 9,
};

enum class DataManagerSetLoadMode : uint8_t {
    REPLACE = 0,
    MERGE = 1,
};

enum class DataManagerDialogMode : uint8_t {
    ASSIGN_SHORTCUT = 0,
    COMMAND_PALETTE = 1,
    SLOT_PICKER = 2,
    SET_LOAD_MODE = 3,
    CONFIRM = 4,
};

inline constexpr DataManagerCommand DEFAULT_MACRO_SHORTCUT_LEFT = DataManagerCommand::MACRO_SAVE_SLOT;
inline constexpr DataManagerCommand DEFAULT_MACRO_SHORTCUT_RIGHT = DataManagerCommand::MACRO_LOAD_SLOT;
inline constexpr DataManagerCommand DEFAULT_SEQ_SHORTCUT_LEFT = DataManagerCommand::SEQ_SAVE_PATTERN_SLOT;
inline constexpr DataManagerCommand DEFAULT_SEQ_SHORTCUT_RIGHT = DataManagerCommand::SEQ_LOAD_PATTERN_SLOT;

inline constexpr const char* dataManagerCommandLabel(DataManagerCommand cmd) {
    switch (cmd) {
        case DataManagerCommand::MACRO_SAVE_SLOT: return "Save Macro";
        case DataManagerCommand::MACRO_LOAD_SLOT: return "Load Macro";
        case DataManagerCommand::MACRO_ERASE_SLOT: return "Erase Macro";
        case DataManagerCommand::SEQ_SAVE_PATTERN_SLOT: return "Save Pattern";
        case DataManagerCommand::SEQ_LOAD_PATTERN_SLOT: return "Load Pattern";
        case DataManagerCommand::SEQ_ERASE_PATTERN_SLOT: return "Erase Pattern";
        case DataManagerCommand::SEQ_SAVE_SET_SLOT: return "Save Set";
        case DataManagerCommand::SEQ_LOAD_SET_SLOT: return "Load Set";
        case DataManagerCommand::SEQ_ERASE_SET_SLOT: return "Erase Set";
        case DataManagerCommand::NONE:
        default:
            return "None";
    }
}

inline constexpr bool dataManagerCommandMatchesContext(DataManagerContext context,
                                                       DataManagerCommand cmd) {
    if (cmd == DataManagerCommand::NONE) return true;

    switch (context) {
        case DataManagerContext::MACRO:
            return cmd == DataManagerCommand::MACRO_SAVE_SLOT ||
                   cmd == DataManagerCommand::MACRO_LOAD_SLOT ||
                   cmd == DataManagerCommand::MACRO_ERASE_SLOT;
        case DataManagerContext::SEQUENCER:
            return cmd == DataManagerCommand::SEQ_SAVE_PATTERN_SLOT ||
                   cmd == DataManagerCommand::SEQ_LOAD_PATTERN_SLOT ||
                   cmd == DataManagerCommand::SEQ_ERASE_PATTERN_SLOT ||
                   cmd == DataManagerCommand::SEQ_SAVE_SET_SLOT ||
                   cmd == DataManagerCommand::SEQ_LOAD_SET_SLOT ||
                   cmd == DataManagerCommand::SEQ_ERASE_SET_SLOT;
    }

    return false;
}

inline constexpr bool dataManagerCommandIsSave(DataManagerCommand cmd) {
    return cmd == DataManagerCommand::MACRO_SAVE_SLOT ||
           cmd == DataManagerCommand::SEQ_SAVE_PATTERN_SLOT ||
           cmd == DataManagerCommand::SEQ_SAVE_SET_SLOT;
}

inline constexpr bool dataManagerCommandIsErase(DataManagerCommand cmd) {
    return cmd == DataManagerCommand::MACRO_ERASE_SLOT ||
           cmd == DataManagerCommand::SEQ_ERASE_PATTERN_SLOT ||
           cmd == DataManagerCommand::SEQ_ERASE_SET_SLOT;
}

inline constexpr bool dataManagerCommandSupportsSetLoadMode(DataManagerCommand cmd) {
    return cmd == DataManagerCommand::SEQ_LOAD_SET_SLOT;
}

inline constexpr DataManagerCommand sanitizeDataManagerShortcut(DataManagerContext context,
                                                                DataManagerCommand candidate,
                                                                DataManagerCommand fallback) {
    if (dataManagerCommandMatchesContext(context, candidate) && candidate != DataManagerCommand::NONE) {
        return candidate;
    }
    return fallback;
}

struct DataManagerDialogState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<DataManagerDialogMode> mode{DataManagerDialogMode::ASSIGN_SHORTCUT};
    oc::state::Signal<int> selectedIndex{0};
    oc::state::Signal<uint8_t> editingShortcutRow{0};

    void reset() {
        visible.set(false);
        mode.set(DataManagerDialogMode::ASSIGN_SHORTCUT);
        selectedIndex.set(0);
        editingShortcutRow.set(0);
    }
};

struct DataManagerState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<DataManagerContext> context{DataManagerContext::MACRO};

    oc::state::Signal<DataManagerCommand> macroShortcutLeft{DEFAULT_MACRO_SHORTCUT_LEFT};
    oc::state::Signal<DataManagerCommand> macroShortcutRight{DEFAULT_MACRO_SHORTCUT_RIGHT};
    oc::state::Signal<DataManagerCommand> seqShortcutLeft{DEFAULT_SEQ_SHORTCUT_LEFT};
    oc::state::Signal<DataManagerCommand> seqShortcutRight{DEFAULT_SEQ_SHORTCUT_RIGHT};

    oc::state::Signal<DataManagerCommand> pendingCommand{DataManagerCommand::NONE};
    oc::state::Signal<uint8_t> pendingSlot{0};
    oc::state::Signal<DataManagerSetLoadMode> pendingSetLoadMode{DataManagerSetLoadMode::REPLACE};

    oc::state::SignalLabel feedback;

    DataManagerDialogState dialog;

    DataManagerState() {
        feedback.set("");
    }

    void resetSession(DataManagerContext activeContext) {
        visible.set(false);
        focusedRow.set(0);
        context.set(activeContext);
        pendingCommand.set(DataManagerCommand::NONE);
        pendingSlot.set(0);
        pendingSetLoadMode.set(DataManagerSetLoadMode::REPLACE);
        dialog.reset();
    }

    DataManagerCommand shortcutForRow(uint8_t row) const {
        const bool left = (row == 0);
        if (context.get() == DataManagerContext::MACRO) {
            return left ? macroShortcutLeft.get() : macroShortcutRight.get();
        }
        return left ? seqShortcutLeft.get() : seqShortcutRight.get();
    }

    uint8_t rowCount() const {
        return 2U;
    }
};

}  // namespace core::state
