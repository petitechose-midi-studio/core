#include "context/standalone/DataManagerPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include "context/standalone/DataManagerPresenterFormatters.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"

namespace core::context::standalone {

FLASHMEM DataManagerPresenter::DataManagerPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& dialogOverlay,
    core::ui::ContextSoftkeyBar& softkeyBar,
    core::ui::TransportBar& transportBar
)
    : state_refs_(stateRefs)
    , overlay_(overlay)
    , dialog_overlay_(dialogOverlay)
    , softkey_bar_(softkeyBar)
    , transport_bar_(transportBar)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("DataManager"),
          &DataManagerPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM bool DataManagerPresenter::bind() {
    bool bound = render_scheduler_.valid();
    overlay_watcher_.bind<&DataManagerPresenter::requestOverlayRender>(
        *this, 0, "DataManager.overlay"
    );
    bound = overlay_watcher_.watchAll(
        state_refs_.dataManager.visible,
        state_refs_.dataManager.focusedRow,
        state_refs_.dataManager.context,
        state_refs_.dataManager.macroShortcutLeft,
        state_refs_.dataManager.macroShortcutRight,
        state_refs_.dataManager.seqShortcutLeft,
        state_refs_.dataManager.seqShortcutRight,
        state_refs_.dataManager.feedback
    ) && bound;

    dialog_watcher_.bind<&DataManagerPresenter::requestDialogRender>(
        *this, 1, "DataManager.dialog"
    );
    bound = dialog_watcher_.watchAll(
        state_refs_.dataManager.flowPhase,
        state_refs_.dataManager.dialog.selectedIndex,
        state_refs_.dataManager.dialog.editingShortcutRow,
        state_refs_.dataManager.context,
        state_refs_.dataManager.pendingCommand,
        state_refs_.dataManager.pendingSlot,
        state_refs_.dataManager.pendingSetLoadMode
    ) && bound;

    softkey_bar_watcher_.bind<&DataManagerPresenter::requestSoftkeyBarRender>(
        *this, 2, "DataManager.softkeyBar"
    );
    bound = softkey_bar_watcher_.watchAll(
        state_refs_.dataManager.visible,
        state_refs_.dataManager.context,
        state_refs_.dataManager.macroShortcutLeft,
        state_refs_.dataManager.macroShortcutRight,
        state_refs_.dataManager.seqShortcutLeft,
        state_refs_.dataManager.seqShortcutRight
    ) && bound;

    render_scheduler_.request(RENDER_OVERLAY | RENDER_DIALOG | RENDER_SOFTKEY_BAR);
    return bound;
}

FLASHMEM void DataManagerPresenter::requestOverlayRender() {
    render_scheduler_.request(RENDER_OVERLAY);
}

FLASHMEM void DataManagerPresenter::requestDialogRender() {
    render_scheduler_.request(RENDER_DIALOG);
}

FLASHMEM void DataManagerPresenter::requestSoftkeyBarRender() {
    render_scheduler_.request(RENDER_SOFTKEY_BAR);
}

FLASHMEM void DataManagerPresenter::drainRenderQueue(void* context, uint32_t flags) {
    auto* self = static_cast<DataManagerPresenter*>(context);
    if (self) self->renderPending(flags);
}

FLASHMEM void DataManagerPresenter::renderPending(uint32_t flags) {
    if ((flags & RENDER_OVERLAY) != 0) renderOverlay();
    if ((flags & RENDER_DIALOG) != 0) renderDialog();
    if ((flags & RENDER_SOFTKEY_BAR) != 0) renderSoftkeyBar();
}

FLASHMEM void DataManagerPresenter::renderOverlay() {
    const auto& dm = state_refs_.dataManager;
    if (!dm.visible.get()) {
        overlay_.render({.visible = false});
        return;
    }

    const auto data = data_manager_presenter::buildOverlayRenderData(state_refs_);

    overlay_.render({
        .title = data.title,
        .meta = data.meta,
        .rows = data.rows.data(),
        .rowCount = static_cast<int>(data.rows.size()),
        .selectedIndex = data.selectedIndex,
        .dimUnselected = false,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void DataManagerPresenter::renderDialog() {
    const auto data = data_manager_presenter::buildDialogRenderData(state_refs_);
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
        .dimUnselected = false,
        .backdropOpacity = LV_OPA_COVER,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void DataManagerPresenter::renderSoftkeyBar() {
    const auto data = data_manager_presenter::buildSoftkeyRenderData(state_refs_);
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
