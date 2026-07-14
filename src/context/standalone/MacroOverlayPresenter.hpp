#pragma once

#include "context/standalone/MacroOverlayInvalidationBindings.hpp"
#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}  // namespace ms::ui

namespace core::ui {
class MacroEditorOverlay;
}

namespace core::context::standalone {

/**
 * Projects macro edit/page selector state into LVGL overlay widgets.
 *
 * The presenter binds signal watchers and renders formatted rows/items only.
 * Macro mutations remain in handlers and macro workflows.
 */
class MacroOverlayPresenter {
public:
    using StateRefs = macro_overlay_presenter::Source;

    MacroOverlayPresenter(StateRefs stateRefs,
                          core::ui::MacroEditorOverlay& macroEditOverlay,
                          ms::ui::VirtualListKeyValueOverlay& macroAutomationOverlay,
                          core::ui::ContextActionStrip& macroEditActionStrip,
                          core::ui::ContextActionStrip& macroAutomationActionStrip,
                          ms::ui::VirtualListSelectorOverlay& macroEditSelectorOverlay,
                          ms::ui::VirtualListSelectorOverlay& pageSelectorOverlay,
                          ms::ui::VirtualListSelectorOverlay& macroTargetSelectorOverlay);

    [[nodiscard]] bool bind();
    void refreshRuntimeTelemetry();

private:
    static void requestRenderFlags(void* context, uint32_t flags);
    static void drainRenderQueue(void* context, uint32_t flags);
    void renderPending(uint32_t flags);
    void renderEdit();
    void renderAutomation();
    void renderEditSelector();
    void renderPageSelector();
    void renderTargetSelector();
    void initializeStaticItems_();

    StateRefs state_refs_;
    core::ui::MacroEditorOverlay& macro_edit_overlay_;
    ms::ui::VirtualListKeyValueOverlay& macro_automation_overlay_;
    core::ui::ContextActionStrip& macro_edit_action_strip_;
    core::ui::ContextActionStrip& macro_automation_action_strip_;
    ms::ui::VirtualListSelectorOverlay& macro_edit_selector_overlay_;
    ms::ui::VirtualListSelectorOverlay& page_selector_overlay_;
    ms::ui::VirtualListSelectorOverlay& macro_target_selector_overlay_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    macro_overlay_invalidation::Bindings invalidation_bindings_;
    // Presenter instances live in EXTMEM; keeping the sizeable preview here
    // avoids rebuilding it through multiple RAM1 stack copies per render.
    macro_overlay_presenter::EditRenderData edit_render_data_{};
    bool static_items_initialized_ = false;
    macro_overlay_presenter::StaticItems static_items_{};
};

}  // namespace core::context::standalone
