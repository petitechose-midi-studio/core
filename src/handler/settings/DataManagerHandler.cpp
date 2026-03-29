#include "DataManagerHandler.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/settings/DataManagerFeedbackFormatter.hpp"
#include "state/DataManagerWorkflow.hpp"

namespace core::handler {

FLASHMEM DataManagerHandler::Services::Services(core::state::CoreState& state) : state_(&state) {}

FLASHMEM uint8_t DataManagerHandler::Services::slotCount(core::state::DataManagerCommand command) const {
    return core::state::DataManagerWorkflow::slotCount(command);
}

FLASHMEM bool DataManagerHandler::Services::slotOccupied(core::state::DataManagerCommand command,
                                                         uint8_t slotIndex) const {
    return core::state::DataManagerWorkflow::slotOccupied(*state_, command, slotIndex);
}

FLASHMEM core::state::DataManagerCommandExecutionResult DataManagerHandler::Services::execute(
    core::state::DataManagerCommand command,
    uint8_t slotIndex,
    core::state::DataManagerSetLoadMode setLoadMode
) const {
    return core::state::DataManagerWorkflow::execute(*state_, command, slotIndex, setLoadMode);
}

FLASHMEM void DataManagerHandler::Services::setShortcut(core::state::DataManagerContext context,
                                                        bool leftButton,
                                                        core::state::DataManagerCommand command) const {
    core::state::DataManagerWorkflow::setShortcut(*state_, context, leftButton, command);
}

FLASHMEM DataManagerHandler::DataManagerHandler(StateRefs state,
                                                Services services,
                                                oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                oc::api::EncoderAPI& encoders,
                                                oc::api::ButtonAPI& buttons,
                                                DataManagerHandler::ViewScopes viewScopes,
                                                oc::type::ScopeID managerOverlayScope,
                                                oc::type::ScopeID dialogOverlayScope)
    : data_manager_(state.dataManager)
    , active_view_(state.activeView)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scopes_(viewScopes)
    , manager_overlay_scope_(managerOverlayScope)
    , dialog_overlay_scope_(dialogOverlayScope) {
    setupBindings();
}

FLASHMEM void DataManagerHandler::setupBindings() {
    const auto navButton = static_cast<oc::type::ButtonID>(Config::ButtonID::NAV);
    const auto leftTopButton = static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP);
    const auto bottomLeftButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_LEFT);
    const auto bottomCenterButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_CENTER);
    const auto bottomRightButton = static_cast<oc::type::ButtonID>(Config::ButtonID::BOTTOM_RIGHT);

    const auto navEncoder = static_cast<oc::type::EncoderID>(Config::EncoderID::NAV);

    oc::type::ScopeID lastBoundScope = 0;
    for (oc::type::ScopeID viewScope : view_scopes_) {
        if (!viewScope || viewScope == lastBoundScope) continue;

        buttons_.button(navButton)
            .longPress(OPEN_LONG_PRESS_MS)
            .scope(viewScope)
            .when([this]() {
                return overlays_.current() == core::ui::OverlayType::NONE;
            })
            .then([this]() { openManager(); });

        lastBoundScope = viewScope;
    }

    // Context overlay scope
    encoders_.encoder(navEncoder)
        .turn()
        .scope(manager_overlay_scope_)
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(navButton)
        .release()
        .scope(manager_overlay_scope_)
        .then([this]() { openShortcutAssignmentDialog_(); });

    buttons_.button(bottomLeftButton)
        .release()
        .scope(manager_overlay_scope_)
        .then([this]() { runShortcut_(true); });

    buttons_.button(bottomCenterButton)
        .release()
        .scope(manager_overlay_scope_)
        .then([this]() { openCommandPaletteDialog_(); });

    buttons_.button(bottomRightButton)
        .release()
        .scope(manager_overlay_scope_)
        .then([this]() { runShortcut_(false); });

    buttons_.button(leftTopButton)
        .release()
        .scope(manager_overlay_scope_)
        .then([this]() { closeManager(); });

    // Dialog scope (assignment, command palette, slot picker, mode picker, confirm)
    encoders_.encoder(navEncoder)
        .turn()
        .scope(dialog_overlay_scope_)
        .then([this](float delta) { navigateDialog_(delta); });

    buttons_.button(navButton)
        .release()
        .scope(dialog_overlay_scope_)
        .then([this]() { applyDialogSelection_(); });

    buttons_.button(leftTopButton)
        .release()
        .scope(dialog_overlay_scope_)
        .then([this]() { closeDialog_(); });
}

FLASHMEM void DataManagerHandler::openManager() {
    auto& dm = data_manager_;
    dm.resetSession(contextForActiveView_());
    dm.visible.set(true);

    ignore_open_release_ = true;
    overlays_.show(core::ui::OverlayType::DATA_MANAGER, false);
}

FLASHMEM void DataManagerHandler::closeManager() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    modal::hideIfCurrent(overlays_, core::ui::OverlayType::DATA_MANAGER_DIALOG);
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::DATA_MANAGER);

    auto& dm = data_manager_;
    dm.dialog.reset();
    dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
    dm.feedback.set("");
}

FLASHMEM void DataManagerHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    auto& dm = data_manager_;
    const int count = static_cast<int>(dm.rowCount());
    const int current = static_cast<int>(dm.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, count);
    dm.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM void DataManagerHandler::openShortcutAssignmentDialog_() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    auto& dm = data_manager_;
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

FLASHMEM void DataManagerHandler::openCommandPaletteDialog_() {
    auto& dm = data_manager_;
    if (dm.dialog.visible.get()) return;

    showDialog_(core::state::DataManagerDialogMode::COMMAND_PALETTE, 0);
}

FLASHMEM void DataManagerHandler::runShortcut_(bool leftButton) {
    auto& dm = data_manager_;
    const auto side = leftButton
                          ? core::state::DataManagerShortcutSide::LEFT
                          : core::state::DataManagerShortcutSide::RIGHT;
    startCommandFlow_(dm.shortcutForSide(side));
}

FLASHMEM void DataManagerHandler::navigateDialog_(float delta) {
    auto& dialog = data_manager_.dialog;
    const auto mode = dialog.mode.get();
    const int count = dialogChoiceCount_(mode);
    int next = dialog.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, dialog, count, next)) {
        return;
    }
    dialog.selectedIndex.set(next);
}

FLASHMEM void DataManagerHandler::applyDialogSelection_() {
    auto& dm = data_manager_;
    auto& dialog = dm.dialog;
    if (!dialog.visible.get()) return;

    const auto mode = dialog.mode.get();

    if (mode == core::state::DataManagerDialogMode::ASSIGN_SHORTCUT) {
        applyShortcutAssignmentSelection_();
        return;
    }

    if (mode == core::state::DataManagerDialogMode::COMMAND_PALETTE) {
        applyCommandPaletteSelection_();
        return;
    }

    if (mode == core::state::DataManagerDialogMode::SLOT_PICKER) {
        applySlotPickerSelection_();
        return;
    }

    if (mode == core::state::DataManagerDialogMode::SET_LOAD_MODE) {
        applySetLoadModeSelection_();
        return;
    }

    if (mode == core::state::DataManagerDialogMode::CONFIRM) {
        applyConfirmSelection_();
    }
}

FLASHMEM void DataManagerHandler::applyShortcutAssignmentSelection_() {
    auto& dm = data_manager_;
    auto& dialog = dm.dialog;
    const auto context = dm.context.get();
    const auto command = core::state::dataManagerCommandAt(context, dialog.selectedIndex.get());
    const uint8_t row = std::min<uint8_t>(dialog.editingShortcutRow.get(), 1U);
    services_.setShortcut(context, row == 0U, command);

    modal::hideOverlayAndResetSelector(overlays_, dialog);
    setFeedback_("Shortcut updated");
}

FLASHMEM void DataManagerHandler::applyCommandPaletteSelection_() {
    auto& dm = data_manager_;
    const auto context = dm.context.get();
    const auto command = core::state::dataManagerCommandAt(context, dm.dialog.selectedIndex.get());
    startCommandFlow_(command);
}

FLASHMEM void DataManagerHandler::applySlotPickerSelection_() {
    auto& dm = data_manager_;
    auto& dialog = dm.dialog;
    const uint8_t slotCount = services_.slotCount(dm.pendingCommand.get());
    if (slotCount == 0U) {
        setFeedback_("No slots");
        modal::hideOverlayAndResetSelector(overlays_, dialog);
        return;
    }

    const int bounded = std::clamp(dialog.selectedIndex.get(), 0, static_cast<int>(slotCount - 1U));
    dm.pendingSlot.set(static_cast<uint8_t>(bounded));

    if (core::state::dataManagerCommandSupportsSetLoadMode(dm.pendingCommand.get())) {
        openSetLoadModeDialog_();
        return;
    }

    const bool needsConfirm =
        core::state::dataManagerCommandIsErase(dm.pendingCommand.get()) ||
        (core::state::dataManagerCommandIsSave(dm.pendingCommand.get()) &&
         services_.slotOccupied(dm.pendingCommand.get(), dm.pendingSlot.get()));
    if (needsConfirm) {
        openConfirmDialog_();
        return;
    }

    executePendingCommand_();
}

FLASHMEM void DataManagerHandler::applySetLoadModeSelection_() {
    auto& dm = data_manager_;
    dm.pendingSetLoadMode.set(
        (dm.dialog.selectedIndex.get() <= 0)
            ? core::state::DataManagerSetLoadMode::REPLACE
            : core::state::DataManagerSetLoadMode::MERGE
    );
    executePendingCommand_();
}

FLASHMEM void DataManagerHandler::applyConfirmSelection_() {
    auto& dm = data_manager_;
    auto& dialog = dm.dialog;
    const bool confirmed = dialog.selectedIndex.get() == 1;
    if (!confirmed) {
        modal::hideOverlayAndResetSelector(overlays_, dialog);
        dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
        setFeedback_("Cancelled");
        return;
    }

    executePendingCommand_();
}

FLASHMEM void DataManagerHandler::closeDialog_() {
    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG) {
        modal::hideOverlayAndResetSelector(overlays_, data_manager_.dialog);
    } else {
        data_manager_.dialog.reset();
    }

    data_manager_.pendingCommand.set(core::state::DataManagerCommand::NONE);
}

FLASHMEM void DataManagerHandler::showDialog_(core::state::DataManagerDialogMode mode,
                                              int selectedIndex,
                                              uint8_t editingShortcutRow) {
    auto& dialog = data_manager_.dialog;
    dialog.mode.set(mode);
    dialog.selectedIndex.set(selectedIndex);
    dialog.editingShortcutRow.set(editingShortcutRow);

    if (!dialog.visible.get()) {
        overlays_.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
    }
}

FLASHMEM int DataManagerHandler::dialogChoiceCount_(core::state::DataManagerDialogMode mode) const {
    switch (mode) {
        case core::state::DataManagerDialogMode::ASSIGN_SHORTCUT:
        case core::state::DataManagerDialogMode::COMMAND_PALETTE:
            return static_cast<int>(
                core::state::dataManagerCommandCount(data_manager_.context.get())
            );
        case core::state::DataManagerDialogMode::SLOT_PICKER:
            return static_cast<int>(
                services_.slotCount(data_manager_.pendingCommand.get())
            );
        case core::state::DataManagerDialogMode::SET_LOAD_MODE:
        case core::state::DataManagerDialogMode::CONFIRM:
            return 2;
        default:
            return 0;
    }
}

FLASHMEM void DataManagerHandler::startCommandFlow_(core::state::DataManagerCommand command) {
    auto& dm = data_manager_;
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

FLASHMEM void DataManagerHandler::openSlotPickerForPendingCommand_() {
    auto& dm = data_manager_;
    const uint8_t slotCount = services_.slotCount(dm.pendingCommand.get());
    if (slotCount == 0U) {
        setFeedback_("No slots");
        return;
    }

    const uint8_t clampedSlot = std::min<uint8_t>(dm.pendingSlot.get(), static_cast<uint8_t>(slotCount - 1U));
    dm.pendingSlot.set(clampedSlot);
    showDialog_(core::state::DataManagerDialogMode::SLOT_PICKER, static_cast<int>(clampedSlot));
}

FLASHMEM void DataManagerHandler::openSetLoadModeDialog_() {
    auto& dm = data_manager_;
    showDialog_(
        core::state::DataManagerDialogMode::SET_LOAD_MODE,
        dm.pendingSetLoadMode.get() == core::state::DataManagerSetLoadMode::REPLACE ? 0 : 1
    );
}

FLASHMEM void DataManagerHandler::openConfirmDialog_() {
    showDialog_(core::state::DataManagerDialogMode::CONFIRM, 0);
}

FLASHMEM void DataManagerHandler::executePendingCommand_() {
    auto& dm = data_manager_;
    const auto command = dm.pendingCommand.get();
    const uint8_t slot = dm.pendingSlot.get();
    const auto setLoadMode = dm.pendingSetLoadMode.get();

    char message[32];
    message[0] = '\0';

    const auto result = services_.execute(command, slot, setLoadMode);
    formatDataManagerCommandExecutionFeedback(
        message,
        sizeof(message),
        command,
        slot,
        setLoadMode,
        result
    );

    setFeedback_(message);

    if (overlays_.current() == core::ui::OverlayType::DATA_MANAGER_DIALOG) {
        modal::hideOverlayAndResetSelector(overlays_, dm.dialog);
    } else {
        dm.dialog.reset();
    }
    dm.pendingCommand.set(core::state::DataManagerCommand::NONE);
}

FLASHMEM core::state::DataManagerContext DataManagerHandler::contextForActiveView_() const {
    if (active_view_.get() == core::ui::ViewType::SEQUENCER) {
        return core::state::DataManagerContext::SEQUENCER;
    }
    return core::state::DataManagerContext::MACRO;
}

FLASHMEM void DataManagerHandler::setFeedback_(const char* message) {
    data_manager_.feedback.set(message ? message : "");
}

}  // namespace core::handler
