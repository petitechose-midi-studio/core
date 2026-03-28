#include "context/standalone/DataManagerPresenter.hpp"

#include <algorithm>
#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/type/TextFormat.hpp>

#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"

namespace core::context::standalone {

DataManagerPresenter::DataManagerPresenter(
    core::state::CoreState& state,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& dialogOverlay,
    core::ui::ContextSoftkeyBar& softkeyBar,
    core::ui::TransportBar& transportBar
)
    : state_(state)
    , overlay_(overlay)
    , dialog_overlay_(dialogOverlay)
    , softkey_bar_(softkeyBar)
    , transport_bar_(transportBar) {}

FLASHMEM void DataManagerPresenter::bind() {
    overlay_watcher_.watchAll(
        [this]() { renderOverlay(); },
        state_.dataManager.visible,
        state_.dataManager.focusedRow,
        state_.dataManager.context,
        state_.dataManager.macroShortcutLeft,
        state_.dataManager.macroShortcutRight,
        state_.dataManager.seqShortcutLeft,
        state_.dataManager.seqShortcutRight,
        state_.dataManager.feedback
    );

    dialog_watcher_.watchAll(
        [this]() { renderDialog(); },
        state_.dataManager.dialog.visible,
        state_.dataManager.dialog.mode,
        state_.dataManager.dialog.selectedIndex,
        state_.dataManager.dialog.editingShortcutRow,
        state_.dataManager.context,
        state_.dataManager.pendingCommand,
        state_.dataManager.pendingSlot,
        state_.dataManager.pendingSetLoadMode
    );

    softkey_bar_watcher_.watchAll(
        [this]() { renderSoftkeyBar(); },
        state_.dataManager.visible,
        state_.dataManager.context,
        state_.dataManager.macroShortcutLeft,
        state_.dataManager.macroShortcutRight,
        state_.dataManager.seqShortcutLeft,
        state_.dataManager.seqShortcutRight
    );
}

FLASHMEM void DataManagerPresenter::renderOverlay() {
    const auto& dm = state_.dataManager;
    if (!dm.visible.get()) {
        overlay_.render({.visible = false});
        return;
    }

    const bool macroContext = dm.context.get() == core::state::DataManagerContext::MACRO;
    const char* title = macroContext ? "MACRO TOOLS" : "SEQUENCER TOOLS";

    const core::state::DataManagerCommand leftCommand = dm.shortcutForRow(0U);
    const core::state::DataManagerCommand rightCommand = dm.shortcutForRow(1U);

    const ms::ui::KeyValueRow rows[] = {
        {.key = "Bottom Left", .value = core::state::dataManagerCommandLabel(leftCommand)},
        {.key = "Bottom Right", .value = core::state::dataManagerCommandLabel(rightCommand)},
    };

    const char* feedback = dm.feedback.get();
    const char* meta = (feedback && feedback[0] != '\0')
                           ? feedback
                           : "NAV=MAP  L/R=RUN  C=ALL";

    uint32_t feedbackHash = 0;
    if (feedback) {
        for (const char* p = feedback; *p; ++p) {
            feedbackHash = (feedbackHash * 131U) + static_cast<uint8_t>(*p);
        }
    }

    const uint32_t dataRevision =
        (static_cast<uint32_t>(dm.context.get()) << 24) |
        (static_cast<uint32_t>(leftCommand) << 16) |
        (static_cast<uint32_t>(rightCommand) << 8) |
        (feedbackHash & 0xFFU);

    overlay_.render({
        .title = title,
        .meta = meta,
        .rows = rows,
        .rowCount = 2,
        .selectedIndex = std::min<uint8_t>(dm.focusedRow.get(), 1U),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

FLASHMEM void DataManagerPresenter::renderDialog() {
    const auto& dm = state_.dataManager;
    const auto& dialog = dm.dialog;

    if (!dialog.visible.get()) {
        dialog_overlay_.render({.visible = false});
        return;
    }

    static const char* const SET_MODE_ITEMS[] = {"REPLACE", "MERGE"};
    static const char* const CONFIRM_ITEMS[] = {"CANCEL", "CONFIRM"};

    const auto context = dm.context.get();
    const int commandCount = static_cast<int>(core::state::dataManagerCommandCount(context));
    for (int i = 0; i < commandCount; ++i) {
        dialog_command_items_[i] = core::state::dataManagerCommandLabel(
            core::state::dataManagerCommandAt(context, i)
        );
    }

    const auto mode = dialog.mode.get();
    const char* title = "COMMAND";
    const char* meta = "";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selected = 0;

    if (mode == core::state::DataManagerDialogMode::ASSIGN_SHORTCUT) {
        title = (dialog.editingShortcutRow.get() == 0) ? "MAP LEFT" : "MAP RIGHT";
        meta = "SELECT COMMAND";
        items = dialog_command_items_.data();
        itemCount = commandCount;
        selected = std::clamp(dialog.selectedIndex.get(), 0, itemCount - 1);
    } else if (mode == core::state::DataManagerDialogMode::COMMAND_PALETTE) {
        title = "COMMANDS";
        meta = "RUN COMMAND";
        items = dialog_command_items_.data();
        itemCount = commandCount;
        selected = std::clamp(dialog.selectedIndex.get(), 0, itemCount - 1);
    } else if (mode == core::state::DataManagerDialogMode::SLOT_PICKER) {
        title = core::state::dataManagerCommandLabel(dm.pendingCommand.get());
        meta = "SELECT SLOT";
        const uint8_t slotCount = core::state::DataManagerWorkflow::slotCount(
            dm.pendingCommand.get()
        );

        itemCount = static_cast<int>(slotCount);
        if (itemCount <= 0) {
            dialog_overlay_.render({.visible = false});
            return;
        }

        const char slotTag = core::state::dataManagerCommandSlotTag(dm.pendingCommand.get());
        const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
        for (int i = 0; i < itemCount; ++i) {
            size_t pos = oc::type::text::appendChar(
                dialog_slot_labels_[i].data(),
                dialog_slot_labels_[i].size(),
                0,
                safeSlotTag
            );
            pos = oc::type::text::appendUnsigned(
                dialog_slot_labels_[i].data(),
                dialog_slot_labels_[i].size(),
                pos,
                static_cast<unsigned>(i + 1),
                2
            );
            oc::type::text::terminate(dialog_slot_labels_[i].data(), dialog_slot_labels_[i].size(), pos);
            dialog_slot_items_[i] = dialog_slot_labels_[i].data();
        }

        items = dialog_slot_items_.data();
        selected = std::clamp(dialog.selectedIndex.get(), 0, itemCount - 1);
    } else if (mode == core::state::DataManagerDialogMode::SET_LOAD_MODE) {
        title = "LOAD SET";
        meta = "MODE";
        items = SET_MODE_ITEMS;
        itemCount = 2;
        selected = std::clamp(dialog.selectedIndex.get(), 0, 1);
    } else if (mode == core::state::DataManagerDialogMode::CONFIRM) {
        title = "CONFIRM";
        const auto cmd = dm.pendingCommand.get();
        const char slotTag = core::state::dataManagerCommandSlotTag(cmd);
        const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
        static char confirmMeta[24];
        if (core::state::dataManagerCommandIsErase(cmd)) {
            size_t pos = oc::type::text::appendString(confirmMeta, sizeof(confirmMeta), 0, "ERASE ");
            pos = oc::type::text::appendChar(confirmMeta, sizeof(confirmMeta), pos, safeSlotTag);
            pos = oc::type::text::appendUnsigned(
                confirmMeta,
                sizeof(confirmMeta),
                pos,
                static_cast<unsigned>(dm.pendingSlot.get() + 1U),
                2
            );
            pos = oc::type::text::appendString(confirmMeta, sizeof(confirmMeta), pos, " ?");
            oc::type::text::terminate(confirmMeta, sizeof(confirmMeta), pos);
        } else {
            size_t pos = oc::type::text::appendString(confirmMeta, sizeof(confirmMeta), 0, "OVERWRITE ");
            pos = oc::type::text::appendChar(confirmMeta, sizeof(confirmMeta), pos, safeSlotTag);
            pos = oc::type::text::appendUnsigned(
                confirmMeta,
                sizeof(confirmMeta),
                pos,
                static_cast<unsigned>(dm.pendingSlot.get() + 1U),
                2
            );
            pos = oc::type::text::appendString(confirmMeta, sizeof(confirmMeta), pos, " ?");
            oc::type::text::terminate(confirmMeta, sizeof(confirmMeta), pos);
        }
        meta = confirmMeta;
        items = CONFIRM_ITEMS;
        itemCount = 2;
        selected = std::clamp(dialog.selectedIndex.get(), 0, 1);
    }

    if (!items || itemCount <= 0) {
        dialog_overlay_.render({.visible = false});
        return;
    }

    const uint32_t dataRevision =
        (static_cast<uint32_t>(mode) << 24) |
        (static_cast<uint32_t>(selected) << 16) |
        (static_cast<uint32_t>(dm.pendingCommand.get()) << 8) |
        static_cast<uint32_t>(dm.pendingSlot.get());

    dialog_overlay_.render({
        .title = title,
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = dataRevision,
    });
}

FLASHMEM void DataManagerPresenter::renderSoftkeyBar() {
    const bool visible = state_.dataManager.visible.get();
    if (!visible) {
        softkey_bar_.hide();
        transport_bar_.show();
        return;
    }

    const auto left = state_.dataManager.shortcutForRow(0U);
    const auto right = state_.dataManager.shortcutForRow(1U);

    char leftLabel[24];
    size_t leftPos = oc::type::text::appendString(leftLabel, sizeof(leftLabel), 0, "L:");
    leftPos = oc::type::text::appendString(
        leftLabel,
        sizeof(leftLabel),
        leftPos,
        core::state::dataManagerCommandLabel(left)
    );
    oc::type::text::terminate(leftLabel, sizeof(leftLabel), leftPos);

    char rightLabel[24];
    size_t rightPos = oc::type::text::appendString(rightLabel, sizeof(rightLabel), 0, "R:");
    rightPos = oc::type::text::appendString(
        rightLabel,
        sizeof(rightLabel),
        rightPos,
        core::state::dataManagerCommandLabel(right)
    );
    oc::type::text::terminate(rightLabel, sizeof(rightLabel), rightPos);

    softkey_bar_.setLabels(leftLabel, "C:Commands", rightLabel);
    softkey_bar_.show();
    transport_bar_.hide();
}

}  // namespace core::context::standalone
