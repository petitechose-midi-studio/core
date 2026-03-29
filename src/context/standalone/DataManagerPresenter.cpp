#include "context/standalone/DataManagerPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include "context/standalone/DataManagerPresenterFormatters.hpp"
#include "state/CoreState.hpp"
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

    const auto data = data_manager_presenter::buildOverlayRenderData(state_);

    overlay_.render({
        .title = data.title,
        .meta = data.meta,
        .rows = data.rows.data(),
        .rowCount = static_cast<int>(data.rows.size()),
        .selectedIndex = data.selectedIndex,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void DataManagerPresenter::renderDialog() {
    const auto data = data_manager_presenter::buildDialogRenderData(state_);
    if (!data.visible) {
        dialog_overlay_.render({.visible = false});
        return;
    }

    dialog_overlay_.render({
        .title = data.title,
        .meta = data.meta,
        .items = data.items,
        .itemCount = data.itemCount,
        .selectedIndex = data.selectedIndex,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void DataManagerPresenter::renderSoftkeyBar() {
    const auto data = data_manager_presenter::buildSoftkeyRenderData(state_);
    if (!data.visible) {
        softkey_bar_.hide();
        transport_bar_.show();
        return;
    }

    softkey_bar_.setLabels(data.leftLabel.data(), "C:Commands", data.rightLabel.data());
    softkey_bar_.show();
    transport_bar_.hide();
}

}  // namespace core::context::standalone
