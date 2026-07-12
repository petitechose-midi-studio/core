#pragma once

#include <oc/state/StaticSignalWatcher.hpp>

#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}  // namespace ms::ui

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
                          ms::ui::VirtualListKeyValueOverlay& macroEditOverlay,
                          ms::ui::VirtualListKeyValueOverlay& macroAutomationOverlay,
                          ms::ui::VirtualListSelectorOverlay& macroEditSelectorOverlay,
                          ms::ui::VirtualListSelectorOverlay& pageSelectorOverlay,
                          ms::ui::VirtualListSelectorOverlay& macroTargetSelectorOverlay);

    [[nodiscard]] bool bind();

private:
    static constexpr uint32_t RENDER_EDIT = 1U << 0;
    static constexpr uint32_t RENDER_AUTOMATION = 1U << 1;
    static constexpr uint32_t RENDER_EDIT_SELECTOR = 1U << 2;
    static constexpr uint32_t RENDER_PAGE_SELECTOR = 1U << 3;
    static constexpr uint32_t RENDER_TARGET_SELECTOR = 1U << 4;

    static void drainRenderQueue(void* context, uint32_t flags);
    void requestEditRender();
    void requestAutomationRender();
    void requestEditSelectorRender();
    void requestPageSelectorRender();
    void requestTargetSelectorRender();
    void renderPending(uint32_t flags);
    void renderEdit();
    void renderAutomation();
    void renderEditSelector();
    void renderPageSelector();
    void renderTargetSelector();
    void initializeStaticItems_();

    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& macro_edit_overlay_;
    ms::ui::VirtualListKeyValueOverlay& macro_automation_overlay_;
    ms::ui::VirtualListSelectorOverlay& macro_edit_selector_overlay_;
    ms::ui::VirtualListSelectorOverlay& page_selector_overlay_;
    ms::ui::VirtualListSelectorOverlay& macro_target_selector_overlay_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    oc::state::StaticWatchGroup<8> edit_watcher_;
    oc::state::StaticWatchGroup<5> automation_watcher_;
    oc::state::StaticWatchGroup<3> edit_selector_watcher_;
    oc::state::StaticWatchGroup<2> page_selector_watcher_;
    oc::state::StaticWatchGroup<2> macro_target_selector_watcher_;
    bool static_items_initialized_ = false;
    macro_overlay_presenter::StaticItems static_items_{};
};

}  // namespace core::context::standalone
