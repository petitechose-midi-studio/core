#include "context/standalone/MacroOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

namespace core::context::standalone {

MacroOverlayPresenter::MacroOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& macroEditOverlay,
    ms::ui::VirtualListKeyValueOverlay& macroAutomationOverlay,
    ms::ui::VirtualListSelectorOverlay& macroEditSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& pageSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& macroTargetSelectorOverlay
)
    : state_refs_(stateRefs)
    , macro_edit_overlay_(macroEditOverlay)
    , macro_automation_overlay_(macroAutomationOverlay)
    , macro_edit_selector_overlay_(macroEditSelectorOverlay)
    , page_selector_overlay_(pageSelectorOverlay)
    , macro_target_selector_overlay_(macroTargetSelectorOverlay) {}

FLASHMEM void MacroOverlayPresenter::bind() {
    edit_watcher_.watchAll(
        [this]() { renderEdit(); },
        state_refs_.macroEdit.visible,
        state_refs_.macroEdit.flowPhase,
        state_refs_.macroEdit.editingIndex,
        state_refs_.macroEdit.tempChannel,
        state_refs_.macroEdit.tempCC,
        state_refs_.macroEdit.focusedRow,
        state_refs_.macroUi.automationRecordingRevision,
        state_refs_.macroUi.automationManualOverrideMask
    );

    automation_watcher_.watchAll(
        [this]() { renderAutomation(); },
        state_refs_.macroEdit.automationVisible,
        state_refs_.macroEdit.editingIndex,
        state_refs_.macroEdit.automationFocusedRow,
        state_refs_.macroUi.automationRecordingRevision,
        state_refs_.macroUi.automationManualOverrideMask
    );

    edit_selector_watcher_.watchAll(
        [this]() { renderEditSelector(); },
        state_refs_.macroEdit.flowPhase,
        state_refs_.macroEdit.selector.editingRow,
        state_refs_.macroEdit.selector.selectedIndex
    );

    page_selector_watcher_.watchAll(
        [this]() { renderPageSelector(); },
        state_refs_.macroEdit.flowPhase,
        state_refs_.pages.selector.selectedIndex
    );

    macro_target_selector_watcher_.watchAll(
        [this]() { renderTargetSelector(); },
        state_refs_.macroEdit.flowPhase,
        state_refs_.macroEdit.macroSelector.selectedIndex
    );
}

FLASHMEM void MacroOverlayPresenter::renderEdit() {
    const bool visible = state_refs_.macroEdit.visible.get() &&
                         state_refs_.macroEdit.flowPhase.get() ==
                             core::state::MacroEditFlowPhase::EDIT;
    if (!visible) {
        macro_edit_overlay_.render({.visible = false});
        return;
    }

    const auto data = macro_overlay_presenter::buildEditRenderData(state_refs_);

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

FLASHMEM void MacroOverlayPresenter::renderAutomation() {
    const auto data = macro_overlay_presenter::buildAutomationRenderData(state_refs_);
    if (!data.visible) {
        macro_automation_overlay_.render({.visible = false});
        return;
    }

    macro_automation_overlay_.render({
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
    const auto data = macro_overlay_presenter::buildEditSelectorRenderData(state_refs_, static_items_);
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
    const auto data = macro_overlay_presenter::buildPageSelectorRenderData(state_refs_);
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
    const auto data = macro_overlay_presenter::buildTargetSelectorRenderData(state_refs_, static_items_);
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
