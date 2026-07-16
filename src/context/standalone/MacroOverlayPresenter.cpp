#include "context/standalone/MacroOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "ui/macro/MacroEditorOverlay.hpp"

namespace core::context::standalone {

FLASHMEM MacroOverlayPresenter::MacroOverlayPresenter(
    StateRefs stateRefs,
    core::ui::MacroEditorOverlay& macroEditOverlay,
    ms::ui::VirtualListKeyValueOverlay& macroAutomationOverlay,
    core::ui::ContextActionStrip& macroEditActionStrip,
    core::ui::ContextActionStrip& macroAutomationActionStrip,
    ms::ui::VirtualListSelectorOverlay& macroEditSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& pageSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& macroTargetSelectorOverlay
)
    : state_refs_(stateRefs)
    , macro_edit_overlay_(macroEditOverlay)
    , macro_automation_overlay_(macroAutomationOverlay)
    , macro_edit_action_strip_(macroEditActionStrip)
    , macro_automation_action_strip_(macroAutomationActionStrip)
    , macro_edit_selector_overlay_(macroEditSelectorOverlay)
    , page_selector_overlay_(pageSelectorOverlay)
    , macro_target_selector_overlay_(macroTargetSelectorOverlay)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("MacroOverlay"),
          &MacroOverlayPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM bool MacroOverlayPresenter::bind() {
    if (!render_scheduler_.valid()) return false;
    return invalidation_bindings_.bind(
        macro_overlay_invalidation::StateRefs{
            state_refs_.macroEdit,
            state_refs_.pages,
            state_refs_.macroUi,
            state_refs_.configRevision,
            state_refs_.clipboard,
        },
        this,
        &MacroOverlayPresenter::requestRenderFlags
    );
}

void MacroOverlayPresenter::refreshRuntimeTelemetry() {
    if (state_refs_.macroEdit.automationVisible.get() &&
        (state_refs_.macroEdit.flowPhase.get() ==
             core::state::MacroEditFlowPhase::MODULATION ||
         state_refs_.macroEdit.flowPhase.get() ==
             core::state::MacroEditFlowPhase::LFO_AUDITION ||
         state_refs_.macroEdit.flowPhase.get() ==
             core::state::MacroEditFlowPhase::MODULATOR_PICKER ||
         state_refs_.macroEdit.flowPhase.get() ==
             core::state::MacroEditFlowPhase::EXISTING_MODULATOR_AUDITION)) {
        render_scheduler_.request(macro_overlay_invalidation::RENDER_AUTOMATION);
    }
}

FLASHMEM void MacroOverlayPresenter::requestRenderFlags(void* context, uint32_t flags) {
    auto* self = static_cast<MacroOverlayPresenter*>(context);
    if (self) self->render_scheduler_.request(flags);
}

FLASHMEM void MacroOverlayPresenter::drainRenderQueue(void* context, uint32_t flags) {
    auto* self = static_cast<MacroOverlayPresenter*>(context);
    if (self) self->renderPending(flags);
}

FLASHMEM void MacroOverlayPresenter::renderPending(uint32_t flags) {
    if ((flags & macro_overlay_invalidation::RENDER_EDIT) != 0) renderEdit();
    if ((flags & macro_overlay_invalidation::RENDER_AUTOMATION) != 0) {
        renderAutomation();
    }
    if ((flags & macro_overlay_invalidation::RENDER_EDIT_SELECTOR) != 0) {
        renderEditSelector();
    }
    if ((flags & macro_overlay_invalidation::RENDER_PAGE_SELECTOR) != 0) {
        renderPageSelector();
    }
    if ((flags & macro_overlay_invalidation::RENDER_TARGET_SELECTOR) != 0) {
        renderTargetSelector();
    }
}

FLASHMEM void MacroOverlayPresenter::renderEdit() {
    const bool visible = state_refs_.macroEdit.visible.get() &&
                         state_refs_.macroEdit.flowPhase.get() ==
                             core::state::MacroEditFlowPhase::EDIT;
    if (!visible) {
        macro_edit_overlay_.render({.visible = false});
        macro_edit_action_strip_.render({.visible = false});
        return;
    }

    macro_overlay_presenter::buildEditRenderData(state_refs_, edit_render_data_);
    const auto& data = edit_render_data_;

    macro_edit_overlay_.render({
        .visible = true,
        .title = data.title.data(),
        .meta = data.meta.data(),
        .destination = data.valueBuffers[0].data(),
        .automation = data.valueBuffers[1].data(),
        .modulation = data.valueBuffers[2].data(),
        .selectedDomain = data.selectedIndex,
        .preview = &data.preview,
        .previewRevision = data.previewRevision,
        .dataRevision = data.dataRevision,
    });
    macro_edit_action_strip_.render(
        macro_overlay_presenter::buildEditActionStripProps(state_refs_)
    );
}

FLASHMEM void MacroOverlayPresenter::renderAutomation() {
    const auto data = macro_overlay_presenter::buildAutomationRenderData(state_refs_);
    if (!data.visible) {
        macro_automation_overlay_.render({.visible = false});
        macro_automation_action_strip_.render({.visible = false});
        return;
    }

    macro_automation_overlay_.render({
        .title = data.title.data(),
        .meta = data.meta.data(),
        .rows = data.rows.data(),
        .rowProvider = data.rowProvider,
        .rowProviderContext = data.rowProviderContext,
        .rowCount = data.rowCount,
        .selectedIndex = data.selectedIndex,
        .dimUnselected = false,
        .visible = true,
        .dataRevision = data.dataRevision,
    });
    macro_automation_action_strip_.render(
        macro_overlay_presenter::buildDetailActionStripProps(state_refs_)
    );
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
