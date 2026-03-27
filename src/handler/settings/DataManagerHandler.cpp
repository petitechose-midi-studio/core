#include "DataManagerHandler.hpp"

#include <algorithm>
#include <cstdio>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/InputIDs.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;

namespace {

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

const char* dataManagerActionFailureLabel(core::state::DataManagerCommandAction action) {
    switch (action) {
        case core::state::DataManagerCommandAction::SAVE:
            return "Save failed";
        case core::state::DataManagerCommandAction::ERASE:
            return "Erase failed";
        case core::state::DataManagerCommandAction::LOAD:
        case core::state::DataManagerCommandAction::NONE:
        default:
            return "Command failed";
    }
}

const char* dataManagerActionSuccessVerb(core::state::DataManagerCommandAction action) {
    switch (action) {
        case core::state::DataManagerCommandAction::SAVE:
            return "Saved";
        case core::state::DataManagerCommandAction::LOAD:
            return "Loaded";
        case core::state::DataManagerCommandAction::ERASE:
            return "Erased";
        case core::state::DataManagerCommandAction::NONE:
        default:
            return "Done";
    }
}

void formatCommandExecutionFeedback(
    char* message,
    size_t messageSize,
    core::state::DataManagerCommand command,
    uint8_t slot,
    core::state::DataManagerSetLoadMode setLoadMode,
    const core::state::DataManagerCommandExecutionResult& result
) {
    if (!message || messageSize == 0U) return;

    message[0] = '\0';

    if (!result.handled || command == core::state::DataManagerCommand::NONE) {
        std::snprintf(message, messageSize, "No command");
        return;
    }

    if (result.isLoadOperation) {
        if (result.loadStatus != core::persistence::SlotLoadStatus::OK) {
            std::snprintf(message,
                          messageSize,
                          "Load %s",
                          slotLoadStatusLabel(result.loadStatus));
            return;
        }

        const char slotTag = core::state::dataManagerCommandSlotTag(command);
        const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
        const char* verb = result.deferredApply ? "Queued" : "Loaded";

        if (core::state::dataManagerCommandSupportsSetLoadMode(command)) {
            const char modeTag =
                (setLoadMode == core::state::DataManagerSetLoadMode::MERGE) ? 'M' : 'R';
            std::snprintf(message,
                          messageSize,
                          "%s %c%02u %c",
                          verb,
                          safeSlotTag,
                          slot + 1U,
                          modeTag);
            return;
        }

        std::snprintf(message,
                      messageSize,
                      "%s %c%02u",
                      verb,
                      safeSlotTag,
                      slot + 1U);
        return;
    }

    const auto action = core::state::dataManagerCommandAction(command);
    if (!result.success) {
        std::snprintf(message,
                      messageSize,
                      "%s",
                      dataManagerActionFailureLabel(action));
        return;
    }

    const char slotTag = core::state::dataManagerCommandSlotTag(command);
    const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
    std::snprintf(message,
                  messageSize,
                  "%s %c%02u",
                  dataManagerActionSuccessVerb(action),
                  safeSlotTag,
                  slot + 1U);
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
    if (!nav::hasTurnDelta(delta)) return;

    auto& dm = state_.dataManager;
    const int count = static_cast<int>(dm.rowCount());
    const int current = static_cast<int>(dm.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, count);
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

    showDialog_(
        core::state::DataManagerDialogMode::ASSIGN_SHORTCUT,
        core::state::dataManagerCommandIndex(context, current),
        row
    );
}

void DataManagerHandler::openCommandPaletteDialog_() {
    auto& dm = state_.dataManager;
    if (dm.dialog.visible.get()) return;

    showDialog_(core::state::DataManagerDialogMode::COMMAND_PALETTE, 0);
}

void DataManagerHandler::runShortcut_(bool leftButton) {
    auto& dm = state_.dataManager;
    const auto side = leftButton
                          ? core::state::DataManagerShortcutSide::LEFT
                          : core::state::DataManagerShortcutSide::RIGHT;
    startCommandFlow_(dm.shortcutForSide(side));
}

void DataManagerHandler::navigateDialog_(float delta) {
    auto& dialog = state_.dataManager.dialog;
    const auto mode = dialog.mode.get();
    const int count = dialogChoiceCount_(mode);
    const int current = dialog.selectedIndex.get();
    int next = current;
    if (!modal::advanceWrappedSelection(delta, dialog.visible.get(), current, count, next)) {
        return;
    }
    dialog.selectedIndex.set(next);
}

void DataManagerHandler::applyDialogSelection_() {
    auto& dm = state_.dataManager;
    auto& dialog = dm.dialog;
    if (!dialog.visible.get()) return;

    const auto mode = dialog.mode.get();

    if (mode == core::state::DataManagerDialogMode::ASSIGN_SHORTCUT) {
        const auto context = dm.context.get();
        const auto command = core::state::dataManagerCommandAt(context, dialog.selectedIndex.get());
        const uint8_t row = std::min<uint8_t>(dialog.editingShortcutRow.get(), 1U);
        core::state::DataManagerWorkflow::setShortcut(state_, context, row == 0U, command);

        overlays_.hide();
        dialog.reset();
        setFeedback_("Shortcut updated");
        return;
    }

    if (mode == core::state::DataManagerDialogMode::COMMAND_PALETTE) {
        const auto context = dm.context.get();
        const auto command = core::state::dataManagerCommandAt(context, dialog.selectedIndex.get());
        startCommandFlow_(command);
        return;
    }

    if (mode == core::state::DataManagerDialogMode::SLOT_PICKER) {
        const uint8_t slotCount = core::state::DataManagerWorkflow::slotCount(
            dm.pendingCommand.get()
        );
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
            core::state::DataManagerWorkflow::slotOccupied(
                state_,
                dm.pendingCommand.get(),
                dm.pendingSlot.get()
            )) {
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
            modal::hideOverlayAndReset(overlays_, [&dialog]() { dialog.reset(); });
            dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
            setFeedback_("Cancelled");
            return;
        }

        executePendingCommand_();
    }
}

void DataManagerHandler::closeDialog_() {
    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG) {
        modal::hideOverlayAndReset(overlays_, [this]() { state_.dataManager.dialog.reset(); });
    } else {
        state_.dataManager.dialog.reset();
    }

    state_.dataManager.pendingCommand.set(core::state::DataManagerCommand::NONE);
}

void DataManagerHandler::showDialog_(core::state::DataManagerDialogMode mode,
                                     int selectedIndex,
                                     uint8_t editingShortcutRow) {
    auto& dialog = state_.dataManager.dialog;
    dialog.mode.set(mode);
    dialog.selectedIndex.set(selectedIndex);
    dialog.editingShortcutRow.set(editingShortcutRow);

    if (!dialog.visible.get()) {
        overlays_.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
    }
}

int DataManagerHandler::dialogChoiceCount_(core::state::DataManagerDialogMode mode) const {
    switch (mode) {
        case core::state::DataManagerDialogMode::ASSIGN_SHORTCUT:
        case core::state::DataManagerDialogMode::COMMAND_PALETTE:
            return static_cast<int>(
                core::state::dataManagerCommandCount(state_.dataManager.context.get())
            );
        case core::state::DataManagerDialogMode::SLOT_PICKER:
            return static_cast<int>(
                core::state::DataManagerWorkflow::slotCount(
                    state_.dataManager.pendingCommand.get()
                )
            );
        case core::state::DataManagerDialogMode::SET_LOAD_MODE:
        case core::state::DataManagerDialogMode::CONFIRM:
            return 2;
        default:
            return 0;
    }
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
    const uint8_t slotCount = core::state::DataManagerWorkflow::slotCount(
        dm.pendingCommand.get()
    );
    if (slotCount == 0U) {
        setFeedback_("No slots");
        return;
    }

    const uint8_t clampedSlot = std::min<uint8_t>(dm.pendingSlot.get(), static_cast<uint8_t>(slotCount - 1U));
    dm.pendingSlot.set(clampedSlot);
    showDialog_(core::state::DataManagerDialogMode::SLOT_PICKER, static_cast<int>(clampedSlot));
}

void DataManagerHandler::openSetLoadModeDialog_() {
    auto& dm = state_.dataManager;
    showDialog_(
        core::state::DataManagerDialogMode::SET_LOAD_MODE,
        dm.pendingSetLoadMode.get() == core::state::DataManagerSetLoadMode::REPLACE ? 0 : 1
    );
}

void DataManagerHandler::openConfirmDialog_() {
    showDialog_(core::state::DataManagerDialogMode::CONFIRM, 0);
}

void DataManagerHandler::executePendingCommand_() {
    auto& dm = state_.dataManager;
    const auto command = dm.pendingCommand.get();
    const uint8_t slot = dm.pendingSlot.get();
    const auto setLoadMode = dm.pendingSetLoadMode.get();

    char message[32];
    message[0] = '\0';

    const auto result = core::state::DataManagerWorkflow::execute(
        state_,
        command,
        slot,
        setLoadMode
    );
    formatCommandExecutionFeedback(message, sizeof(message), command, slot, setLoadMode, result);

    setFeedback_(message);

    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG) {
        modal::hideOverlayAndReset(overlays_, [&dm]() { dm.dialog.reset(); });
    } else {
        dm.dialog.reset();
    }
    dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
}

core::state::DataManagerContext DataManagerHandler::contextForActiveView_() const {
    if (state_.activeView.get() == core::ui::ViewType::SEQUENCER) {
        return core::state::DataManagerContext::SEQUENCER;
    }
    return core::state::DataManagerContext::MACRO;
}

void DataManagerHandler::setFeedback_(const char* message) {
    state_.dataManager.feedback.set(message ? message : "");
}

}  // namespace core::handler
