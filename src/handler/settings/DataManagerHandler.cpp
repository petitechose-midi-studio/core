#include "DataManagerHandler.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include <oc/util/Index.hpp>

#include <config/InputIDs.hpp>

namespace core::handler {

using oc::ui::lvgl::scope;
using oc::util::wrapIndex;

namespace {

constexpr std::array<core::state::DataManagerCommand, 3> MACRO_COMMANDS = {
    core::state::DataManagerCommand::MACRO_SAVE_SLOT,
    core::state::DataManagerCommand::MACRO_LOAD_SLOT,
    core::state::DataManagerCommand::MACRO_ERASE_SLOT,
};

constexpr std::array<core::state::DataManagerCommand, 6> SEQ_COMMANDS = {
    core::state::DataManagerCommand::SEQ_SAVE_PATTERN_SLOT,
    core::state::DataManagerCommand::SEQ_LOAD_PATTERN_SLOT,
    core::state::DataManagerCommand::SEQ_ERASE_PATTERN_SLOT,
    core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT,
    core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT,
    core::state::DataManagerCommand::SEQ_ERASE_SET_SLOT,
};

const char* slotLoadStatusLabel(core::persistence::SlotLoadStatus status) {
    using core::persistence::SlotLoadStatus;
    switch (status) {
        case SlotLoadStatus::OK: return "OK";
        case SlotLoadStatus::EMPTY: return "EMPTY";
        case SlotLoadStatus::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case SlotLoadStatus::CRC_MISMATCH: return "CRC";
        case SlotLoadStatus::HEADER_MISMATCH: return "HEADER";
        case SlotLoadStatus::IO_ERROR: return "IO";
        case SlotLoadStatus::STORAGE_UNAVAILABLE: return "NO_STORAGE";
        case SlotLoadStatus::CAPACITY_TOO_SMALL: return "CAPACITY";
        default:
            return "ERROR";
    }
}

}  // namespace

DataManagerHandler::DataManagerHandler(core::state::CoreState& state,
                                       oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                       oc::api::EncoderAPI& encoders,
                                       oc::api::ButtonAPI& buttons,
                                       DataManagerHandler::ViewScopes viewScopes,
                                       lv_obj_t* managerOverlayScope,
                                       lv_obj_t* dialogOverlayScope)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scopes_(viewScopes)
    , manager_overlay_scope_(managerOverlayScope)
    , dialog_overlay_scope_(dialogOverlayScope) {
    setupBindings();
}

void DataManagerHandler::setupBindings() {
    const auto navButton = static_cast<oc::type::ButtonID>(Config::ButtonID::NAV);
    const auto leftTopButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP);
    const auto bottomLeftButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_LEFT);
    const auto bottomCenterButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_CENTER);
    const auto bottomRightButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_RIGHT);

    const auto navEncoder = static_cast<oc::type::EncoderID>(Config::EncoderID::NAV);

    lv_obj_t* lastBoundScope = nullptr;
    for (auto* viewScope : view_scopes_) {
        if (!viewScope || viewScope == lastBoundScope) continue;

        buttons_.button(navButton)
            .longPress(OPEN_LONG_PRESS_MS)
            .scope(scope(viewScope))
            .when([this]() {
                return overlays_.current() == core::ui::OverlayType::NONE;
            })
            .then([this]() { openManager(); });

        lastBoundScope = viewScope;
    }

    // Context overlay scope
    encoders_.encoder(navEncoder)
        .turn()
        .scope(scope(manager_overlay_scope_))
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(navButton)
        .release()
        .scope(scope(manager_overlay_scope_))
        .then([this]() { openShortcutAssignmentDialog_(); });

    buttons_.button(bottomLeftButton)
        .release()
        .scope(scope(manager_overlay_scope_))
        .then([this]() { runShortcut_(true); });

    buttons_.button(bottomCenterButton)
        .release()
        .scope(scope(manager_overlay_scope_))
        .then([this]() { openCommandPaletteDialog_(); });

    buttons_.button(bottomRightButton)
        .release()
        .scope(scope(manager_overlay_scope_))
        .then([this]() { runShortcut_(false); });

    buttons_.button(leftTopButton)
        .release()
        .scope(scope(manager_overlay_scope_))
        .then([this]() { closeManager(); });

    // Dialog scope (assignment, command palette, slot picker, mode picker, confirm)
    encoders_.encoder(navEncoder)
        .turn()
        .scope(scope(dialog_overlay_scope_))
        .then([this](float delta) { navigateDialog_(delta); });

    buttons_.button(navButton)
        .release()
        .scope(scope(dialog_overlay_scope_))
        .then([this]() { applyDialogSelection_(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(scope(dialog_overlay_scope_))
        .then([this]() { closeDialog_(); });

    OC_LOG_DEBUG("[DataManagerHandler] Bindings setup complete");
}

void DataManagerHandler::openManager() {
    auto& dm = state_.dataManager;
    dm.resetSession(contextForActiveView_());
    dm.visible.set(true);

    ignore_open_release_ = true;
    overlays_.show(core::ui::OverlayType::DATA_MANAGER, false);
}

void DataManagerHandler::closeManager() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG) {
        overlays_.hide();
    }

    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER) {
        overlays_.hide();
    }

    auto& dm = state_.dataManager;
    dm.dialog.reset();
    dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
    dm.feedback.set("");
}

void DataManagerHandler::moveFocus(float delta) {
    if (delta == 0.0f) return;

    const int step = (delta > 0.0f) ? 1 : -1;
    auto& dm = state_.dataManager;
    const int count = static_cast<int>(dm.rowCount());
    const int current = static_cast<int>(dm.focusedRow.get());
    const int next = wrapIndex(current + step, count);
    dm.focusedRow.set(static_cast<uint8_t>(next));
}

void DataManagerHandler::openShortcutAssignmentDialog_() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    auto& dm = state_.dataManager;
    if (dm.dialog.visible.get()) return;

    const uint8_t row = std::min<uint8_t>(dm.focusedRow.get(), 1U);
    const auto context = dm.context.get();
    const auto current = dm.shortcutForRow(row);

    dm.dialog.mode.set(core::state::DataManagerDialogMode::ASSIGN_SHORTCUT);
    dm.dialog.editingShortcutRow.set(row);
    dm.dialog.selectedIndex.set(commandIndexForContext_(context, current));

    overlays_.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
}

void DataManagerHandler::openCommandPaletteDialog_() {
    auto& dm = state_.dataManager;
    if (dm.dialog.visible.get()) return;

    dm.dialog.mode.set(core::state::DataManagerDialogMode::COMMAND_PALETTE);
    dm.dialog.selectedIndex.set(0);
    dm.dialog.editingShortcutRow.set(0);

    overlays_.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
}

void DataManagerHandler::runShortcut_(bool leftButton) {
    auto& dm = state_.dataManager;
    const uint8_t row = leftButton ? 0U : 1U;
    startCommandFlow_(dm.shortcutForRow(row));
}

void DataManagerHandler::navigateDialog_(float delta) {
    if (delta == 0.0f) return;

    auto& dialog = state_.dataManager.dialog;
    if (!dialog.visible.get()) return;

    int count = 0;
    const auto mode = dialog.mode.get();
    if (mode == core::state::DataManagerDialogMode::ASSIGN_SHORTCUT ||
        mode == core::state::DataManagerDialogMode::COMMAND_PALETTE) {
        count = static_cast<int>(commandCountForContext_(state_.dataManager.context.get()));
    } else if (mode == core::state::DataManagerDialogMode::SLOT_PICKER) {
        count = static_cast<int>(slotCountForCommand_(state_.dataManager.pendingCommand.get()));
    } else if (mode == core::state::DataManagerDialogMode::SET_LOAD_MODE) {
        count = 2;
    } else if (mode == core::state::DataManagerDialogMode::CONFIRM) {
        count = 2;
    }

    if (count <= 0) return;

    const int step = (delta > 0.0f) ? 1 : -1;
    const int current = dialog.selectedIndex.get();
    dialog.selectedIndex.set(wrapIndex(current + step, count));
}

void DataManagerHandler::applyDialogSelection_() {
    auto& dm = state_.dataManager;
    auto& dialog = dm.dialog;
    if (!dialog.visible.get()) return;

    const auto mode = dialog.mode.get();

    if (mode == core::state::DataManagerDialogMode::ASSIGN_SHORTCUT) {
        const auto context = dm.context.get();
        const auto command = commandAtIndexForContext_(context, dialog.selectedIndex.get());
        const uint8_t row = std::min<uint8_t>(dialog.editingShortcutRow.get(), 1U);
        state_.setDataManagerShortcut(context, row == 0U, command);

        overlays_.hide();
        dialog.reset();
        setFeedback_("Shortcut updated");
        return;
    }

    if (mode == core::state::DataManagerDialogMode::COMMAND_PALETTE) {
        const auto context = dm.context.get();
        const auto command = commandAtIndexForContext_(context, dialog.selectedIndex.get());
        startCommandFlow_(command);
        return;
    }

    if (mode == core::state::DataManagerDialogMode::SLOT_PICKER) {
        const uint8_t slotCount = slotCountForCommand_(dm.pendingCommand.get());
        if (slotCount == 0U) {
            setFeedback_("No slots");
            overlays_.hide();
            dialog.reset();
            return;
        }

        const int bounded = std::clamp(dialog.selectedIndex.get(), 0, static_cast<int>(slotCount - 1U));
        dm.pendingSlot.set(static_cast<uint8_t>(bounded));

        if (core::state::dataManagerCommandSupportsSetLoadMode(dm.pendingCommand.get())) {
            openSetLoadModeDialog_();
            return;
        }

        if (core::state::dataManagerCommandIsErase(dm.pendingCommand.get())) {
            openConfirmDialog_();
            return;
        }

        if (core::state::dataManagerCommandIsSave(dm.pendingCommand.get()) &&
            slotOccupiedForCommand_(dm.pendingCommand.get(), dm.pendingSlot.get())) {
            openConfirmDialog_();
            return;
        }

        executePendingCommand_();
        return;
    }

    if (mode == core::state::DataManagerDialogMode::SET_LOAD_MODE) {
        dm.pendingSetLoadMode.set(
            (dialog.selectedIndex.get() <= 0)
                ? core::state::DataManagerSetLoadMode::REPLACE
                : core::state::DataManagerSetLoadMode::MERGE
        );
        executePendingCommand_();
        return;
    }

    if (mode == core::state::DataManagerDialogMode::CONFIRM) {
        const bool confirmed = dialog.selectedIndex.get() == 1;
        if (!confirmed) {
            overlays_.hide();
            dialog.reset();
            dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
            setFeedback_("Cancelled");
            return;
        }

        executePendingCommand_();
    }
}

void DataManagerHandler::closeDialog_() {
    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG) {
        overlays_.hide();
    }

    state_.dataManager.dialog.reset();
    state_.dataManager.pendingCommand.set(core::state::DataManagerCommand::NONE);
}

void DataManagerHandler::startCommandFlow_(core::state::DataManagerCommand command) {
    auto& dm = state_.dataManager;
    if (command == core::state::DataManagerCommand::NONE) {
        setFeedback_("No command mapped");
        return;
    }

    if (!core::state::dataManagerCommandMatchesContext(dm.context.get(), command)) {
        setFeedback_("Command not available");
        return;
    }

    dm.pendingCommand.set(command);
    dm.pendingSetLoadMode.set(core::state::DataManagerSetLoadMode::REPLACE);

    openSlotPickerForPendingCommand_();
}

void DataManagerHandler::openSlotPickerForPendingCommand_() {
    auto& dm = state_.dataManager;
    const uint8_t slotCount = slotCountForCommand_(dm.pendingCommand.get());
    if (slotCount == 0U) {
        setFeedback_("No slots");
        return;
    }

    const uint8_t clampedSlot = std::min<uint8_t>(dm.pendingSlot.get(), static_cast<uint8_t>(slotCount - 1U));
    dm.pendingSlot.set(clampedSlot);
    dm.dialog.mode.set(core::state::DataManagerDialogMode::SLOT_PICKER);
    dm.dialog.selectedIndex.set(static_cast<int>(clampedSlot));

    if (!dm.dialog.visible.get()) {
        overlays_.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
    }
}

void DataManagerHandler::openSetLoadModeDialog_() {
    auto& dm = state_.dataManager;
    dm.dialog.mode.set(core::state::DataManagerDialogMode::SET_LOAD_MODE);
    dm.dialog.selectedIndex.set(dm.pendingSetLoadMode.get() == core::state::DataManagerSetLoadMode::REPLACE ? 0 : 1);

    if (!dm.dialog.visible.get()) {
        overlays_.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
    }
}

void DataManagerHandler::openConfirmDialog_() {
    auto& dm = state_.dataManager;
    dm.dialog.mode.set(core::state::DataManagerDialogMode::CONFIRM);
    dm.dialog.selectedIndex.set(0);  // Default to cancel

    if (!dm.dialog.visible.get()) {
        overlays_.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
    }
}

void DataManagerHandler::executePendingCommand_() {
    auto& dm = state_.dataManager;
    const auto command = dm.pendingCommand.get();
    const uint8_t slot = dm.pendingSlot.get();

    char message[32];
    message[0] = '\0';

    using core::persistence::SlotLoadStatus;
    switch (command) {
        case core::state::DataManagerCommand::MACRO_SAVE_SLOT: {
            const bool ok = state_.saveMacroLibrarySlot(slot);
            std::snprintf(message, sizeof(message), ok ? "Saved M%02u" : "Save failed", slot + 1);
            break;
        }
        case core::state::DataManagerCommand::MACRO_LOAD_SLOT: {
            const SlotLoadStatus status = state_.loadMacroLibrarySlot(slot);
            if (status == SlotLoadStatus::OK) {
                std::snprintf(message, sizeof(message), "Loaded M%02u", slot + 1);
            } else {
                std::snprintf(message, sizeof(message), "Load %s", slotLoadStatusLabel(status));
            }
            break;
        }
        case core::state::DataManagerCommand::MACRO_ERASE_SLOT: {
            const bool ok = state_.eraseMacroLibrarySlot(slot);
            std::snprintf(message, sizeof(message), ok ? "Erased M%02u" : "Erase failed", slot + 1);
            break;
        }
        case core::state::DataManagerCommand::SEQ_SAVE_PATTERN_SLOT: {
            const bool ok = state_.saveSequencerPatternSlot(slot);
            std::snprintf(message, sizeof(message), ok ? "Saved P%02u" : "Save failed", slot + 1);
            break;
        }
        case core::state::DataManagerCommand::SEQ_LOAD_PATTERN_SLOT: {
            const SlotLoadStatus status = state_.loadSequencerPatternSlot(slot);
            if (status == SlotLoadStatus::OK) {
                std::snprintf(message, sizeof(message), "Loaded P%02u", slot + 1);
            } else {
                std::snprintf(message, sizeof(message), "Load %s", slotLoadStatusLabel(status));
            }
            break;
        }
        case core::state::DataManagerCommand::SEQ_ERASE_PATTERN_SLOT: {
            const bool ok = state_.eraseSequencerPatternSlot(slot);
            std::snprintf(message, sizeof(message), ok ? "Erased P%02u" : "Erase failed", slot + 1);
            break;
        }
        case core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT: {
            const bool ok = state_.saveSequencerSetSlot(slot);
            std::snprintf(message, sizeof(message), ok ? "Saved S%02u" : "Save failed", slot + 1);
            break;
        }
        case core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT: {
            const bool merge = dm.pendingSetLoadMode.get() == core::state::DataManagerSetLoadMode::MERGE;
            const SlotLoadStatus status = state_.loadSequencerSetSlot(slot, merge);
            if (status == SlotLoadStatus::OK) {
                std::snprintf(message, sizeof(message), merge ? "Loaded S%02u M" : "Loaded S%02u R", slot + 1);
            } else {
                std::snprintf(message, sizeof(message), "Load %s", slotLoadStatusLabel(status));
            }
            break;
        }
        case core::state::DataManagerCommand::SEQ_ERASE_SET_SLOT: {
            const bool ok = state_.eraseSequencerSetSlot(slot);
            std::snprintf(message, sizeof(message), ok ? "Erased S%02u" : "Erase failed", slot + 1);
            break;
        }
        case core::state::DataManagerCommand::NONE:
        default:
            std::snprintf(message, sizeof(message), "No command");
            break;
    }

    setFeedback_(message);

    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG) {
        overlays_.hide();
    }
    dm.dialog.reset();
    dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
}

core::state::DataManagerContext DataManagerHandler::contextForActiveView_() const {
    if (state_.activeView.get() == core::ui::ViewType::SEQUENCER) {
        return core::state::DataManagerContext::SEQUENCER;
    }
    return core::state::DataManagerContext::MACRO;
}

std::size_t DataManagerHandler::commandCountForContext_(core::state::DataManagerContext context) const {
    return (context == core::state::DataManagerContext::MACRO)
               ? MACRO_COMMANDS.size()
               : SEQ_COMMANDS.size();
}

core::state::DataManagerCommand DataManagerHandler::commandAtIndexForContext_(
    core::state::DataManagerContext context,
    int index
) const {
    if (context == core::state::DataManagerContext::MACRO) {
        const int bounded = std::clamp(index, 0, static_cast<int>(MACRO_COMMANDS.size()) - 1);
        return MACRO_COMMANDS[static_cast<std::size_t>(bounded)];
    }

    const int bounded = std::clamp(index, 0, static_cast<int>(SEQ_COMMANDS.size()) - 1);
    return SEQ_COMMANDS[static_cast<std::size_t>(bounded)];
}

int DataManagerHandler::commandIndexForContext_(core::state::DataManagerContext context,
                                                core::state::DataManagerCommand command) const {
    if (context == core::state::DataManagerContext::MACRO) {
        for (std::size_t i = 0; i < MACRO_COMMANDS.size(); ++i) {
            if (MACRO_COMMANDS[i] == command) {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    for (std::size_t i = 0; i < SEQ_COMMANDS.size(); ++i) {
        if (SEQ_COMMANDS[i] == command) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

uint8_t DataManagerHandler::slotCountForCommand_(core::state::DataManagerCommand command) const {
    switch (command) {
        case core::state::DataManagerCommand::MACRO_SAVE_SLOT:
        case core::state::DataManagerCommand::MACRO_LOAD_SLOT:
        case core::state::DataManagerCommand::MACRO_ERASE_SLOT:
            return core::persistence::MacroPersistence::LIBRARY_SLOT_COUNT;

        case core::state::DataManagerCommand::SEQ_SAVE_PATTERN_SLOT:
        case core::state::DataManagerCommand::SEQ_LOAD_PATTERN_SLOT:
        case core::state::DataManagerCommand::SEQ_ERASE_PATTERN_SLOT:
            return core::persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT;

        case core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT:
        case core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT:
        case core::state::DataManagerCommand::SEQ_ERASE_SET_SLOT:
            return core::persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT;

        case core::state::DataManagerCommand::NONE:
        default:
            return 0;
    }
}

bool DataManagerHandler::slotOccupiedForCommand_(core::state::DataManagerCommand command,
                                                 uint8_t slot) const {
    if (command == core::state::DataManagerCommand::MACRO_SAVE_SLOT ||
        command == core::state::DataManagerCommand::MACRO_ERASE_SLOT) {
        core::state::macro::MacroPagesState temp;
        temp.initDefaults();
        return state_.macroPersistence.loadLibrarySlot(slot, temp) == core::persistence::SlotLoadStatus::OK;
    }

    if (command == core::state::DataManagerCommand::SEQ_SAVE_PATTERN_SLOT ||
        command == core::state::DataManagerCommand::SEQ_ERASE_PATTERN_SLOT) {
        core::state::sequencer::SequencerState temp;
        temp.reset();
        return state_.sequencerPersistence.loadPatternSlot(slot, temp) == core::persistence::SlotLoadStatus::OK;
    }

    if (command == core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT ||
        command == core::state::DataManagerCommand::SEQ_ERASE_SET_SLOT) {
        core::state::sequencer::SequencerState temp;
        temp.reset();
        return state_.sequencerPersistence.loadSetSlot(slot, temp) == core::persistence::SlotLoadStatus::OK;
    }

    return false;
}

void DataManagerHandler::setFeedback_(const char* message) {
    state_.dataManager.feedback.set(message ? message : "");
}

}  // namespace core::handler
