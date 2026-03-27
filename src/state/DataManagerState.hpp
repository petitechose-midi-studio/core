#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

#include "DataManagerCatalog.hpp"

namespace core::state {

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

    DataManagerCommand shortcutForSide(DataManagerShortcutSide side) const {
        const bool left = side == DataManagerShortcutSide::LEFT;
        if (context.get() == DataManagerContext::MACRO) {
            return left ? macroShortcutLeft.get() : macroShortcutRight.get();
        }
        return left ? seqShortcutLeft.get() : seqShortcutRight.get();
    }

    DataManagerCommand shortcutForRow(uint8_t row) const {
        return shortcutForSide((row == 0U) ? DataManagerShortcutSide::LEFT
                                           : DataManagerShortcutSide::RIGHT);
    }

    uint8_t rowCount() const {
        return 2U;
    }
};

}  // namespace core::state
