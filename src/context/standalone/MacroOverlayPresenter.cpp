#include "context/standalone/MacroOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

namespace core::context::standalone {

FLASHMEM MacroOverlayPresenter::MacroOverlayPresenter(
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
    , macro_target_selector_overlay_(macroTargetSelectorOverlay)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("MacroOverlay"),
          &MacroOverlayPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM bool MacroOverlayPresenter::bind() {
    bool bound = render_scheduler_.valid();
    edit_watcher_.bind<&MacroOverlayPresenter::requestEditRender>(
        *this, 0, "MacroOverlay.edit"
    );
    bound = edit_watcher_.watchAll(
        state_refs_.macroEdit.visible,
        state_refs_.macroEdit.flowPhase,
        state_refs_.macroEdit.editingIndex,
        state_refs_.macroEdit.tempChannel,
        state_refs_.macroEdit.tempCC,
        state_refs_.macroEdit.focusedRow,
        state_refs_.macroUi.automationRecordingRevision,
        state_refs_.macroUi.automationManualOverrideMask
    ) && bound;

    automation_watcher_.bind<&MacroOverlayPresenter::requestAutomationRender>(
        *this, 1, "MacroOverlay.automation"
    );
    bound = automation_watcher_.watchAll(
        state_refs_.macroEdit.automationVisible,
        state_refs_.macroEdit.editingIndex,
        state_refs_.macroEdit.automationFocusedRow,
        state_refs_.macroUi.automationRecordingRevision,
        state_refs_.macroUi.automationManualOverrideMask
    ) && bound;

    edit_selector_watcher_.bind<&MacroOverlayPresenter::requestEditSelectorRender>(
        *this, 2, "MacroOverlay.editSelector"
    );
    bound = edit_selector_watcher_.watchAll(
        state_refs_.macroEdit.flowPhase,
        state_refs_.macroEdit.selector.editingRow,
        state_refs_.macroEdit.selector.selectedIndex
    ) && bound;

    page_selector_watcher_.bind<&MacroOverlayPresenter::requestPageSelectorRender>(
        *this, 3, "MacroOverlay.pageSelector"
    );
    bound = page_selector_watcher_.watchAll(
        state_refs_.macroEdit.flowPhase,
        state_refs_.pages.selector.selectedIndex
    ) && bound;

    macro_target_selector_watcher_.bind<&MacroOverlayPresenter::requestTargetSelectorRender>(
        *this, 4, "MacroOverlay.targetSelector"
    );
    bound = macro_target_selector_watcher_.watchAll(
        state_refs_.macroEdit.flowPhase,
        state_refs_.macroEdit.macroSelector.selectedIndex
    ) && bound;
    return bound;
}

FLASHMEM void MacroOverlayPresenter::requestEditRender() {
    render_scheduler_.request(RENDER_EDIT);
}

FLASHMEM void MacroOverlayPresenter::requestAutomationRender() {
    render_scheduler_.request(RENDER_AUTOMATION);
}

FLASHMEM void MacroOverlayPresenter::requestEditSelectorRender() {
    render_scheduler_.request(RENDER_EDIT_SELECTOR);
}

FLASHMEM void MacroOverlayPresenter::requestPageSelectorRender() {
    render_scheduler_.request(RENDER_PAGE_SELECTOR);
}

FLASHMEM void MacroOverlayPresenter::requestTargetSelectorRender() {
    render_scheduler_.request(RENDER_TARGET_SELECTOR);
}

FLASHMEM void MacroOverlayPresenter::drainRenderQueue(void* context, uint32_t flags) {
    auto* self = static_cast<MacroOverlayPresenter*>(context);
    if (self) self->renderPending(flags);
}

FLASHMEM void MacroOverlayPresenter::renderPending(uint32_t flags) {
    if ((flags & RENDER_EDIT) != 0) renderEdit();
    if ((flags & RENDER_AUTOMATION) != 0) renderAutomation();
    if ((flags & RENDER_EDIT_SELECTOR) != 0) renderEditSelector();
    if ((flags & RENDER_PAGE_SELECTOR) != 0) renderPageSelector();
    if ((flags & RENDER_TARGET_SELECTOR) != 0) renderTargetSelector();
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
