#include "context/standalone/MacroOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include "state/CoreState.hpp"

namespace core::context::standalone {

MacroOverlayPresenter::MacroOverlayPresenter(
    core::state::CoreState& state,
    ms::ui::VirtualListKeyValueOverlay& macroEditOverlay,
    ms::ui::VirtualListSelectorOverlay& macroEditSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& pageSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& macroTargetSelectorOverlay
)
    : state_(state)
    , macro_edit_overlay_(macroEditOverlay)
    , macro_edit_selector_overlay_(macroEditSelectorOverlay)
    , page_selector_overlay_(pageSelectorOverlay)
    , macro_target_selector_overlay_(macroTargetSelectorOverlay) {}

FLASHMEM void MacroOverlayPresenter::bind() {
    edit_watcher_.watchAll(
        [this]() { renderEdit(); },
        state_.macroEdit.visible,
        state_.macroEdit.editingIndex,
        state_.macroEdit.tempChannel,
        state_.macroEdit.tempCC,
        state_.macroEdit.focusedRow,
        state_.configRevision
    );

    edit_selector_watcher_.watchAll(
        [this]() { renderEditSelector(); },
        state_.macroEdit.selector.visible,
        state_.macroEdit.selector.editingRow,
        state_.macroEdit.selector.selectedIndex
    );

    page_selector_watcher_.watchAll(
        [this]() { renderPageSelector(); },
        state_.pages.selector.visible,
        state_.pages.selector.selectedIndex,
        state_.configRevision
    );

    macro_target_selector_watcher_.watchAll(
        [this]() { renderTargetSelector(); },
        state_.macroEdit.macroSelector.visible,
        state_.macroEdit.macroSelector.selectedIndex
    );
}

FLASHMEM void MacroOverlayPresenter::renderEdit() {
    const bool visible = state_.macroEdit.visible.get();
    if (!visible) {
        macro_edit_overlay_.render({.visible = false});
        return;
    }

    const auto data = macro_overlay_presenter::buildEditRenderData(state_);

    macro_edit_overlay_.render({
        .title = data.title.data(),
        .meta = data.meta.data(),
        .rows = data.rows.data(),
        .rowCount = static_cast<int>(data.rows.size()),
        .selectedIndex = data.selectedIndex,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
}

FLASHMEM void MacroOverlayPresenter::renderEditSelector() {
    initializeStaticItems_();
    const auto data = macro_overlay_presenter::buildEditSelectorRenderData(state_, static_items_);
    if (!data.visible) {
        macro_edit_selector_overlay_.render({.visible = false});
        return;
    }

    macro_edit_selector_overlay_.render({
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

FLASHMEM void MacroOverlayPresenter::renderPageSelector() {
    const auto data = macro_overlay_presenter::buildPageSelectorRenderData(state_);
    if (!data.visible) {
        page_selector_overlay_.render({.visible = false});
        return;
    }

    page_selector_overlay_.render({
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

FLASHMEM void MacroOverlayPresenter::renderTargetSelector() {
    initializeStaticItems_();
    const auto data = macro_overlay_presenter::buildTargetSelectorRenderData(state_, static_items_);
    if (!data.visible) {
        macro_target_selector_overlay_.render({.visible = false});
        return;
    }

    macro_target_selector_overlay_.render({
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

FLASHMEM void MacroOverlayPresenter::initializeStaticItems_() {
    if (static_items_initialized_) return;
    macro_overlay_presenter::initializeStaticItems(static_items_);
    static_items_initialized_ = true;
}

}  // namespace core::context::standalone
