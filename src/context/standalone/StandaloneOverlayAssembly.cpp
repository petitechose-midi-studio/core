#include "context/standalone/StandaloneOverlayAssembly.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include <config/App.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include "state/CoreState.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::context::standalone {

FLASHMEM StandaloneOverlayAssembly::StandaloneOverlayAssembly(
    core::state::CoreState& state,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* overlayRoot,
    ActiveViewScopeProvider activeViewScopeProvider
)
    : core_state_(state) {
    createOverlayController(buttons, std::move(activeViewScopeProvider));
    createViewSelectorOverlay(overlayRoot);
}

FLASHMEM StandaloneOverlayAssembly::~StandaloneOverlayAssembly() = default;

FLASHMEM oc::context::OverlayManager<core::ui::OverlayType>&
StandaloneOverlayAssembly::controller() const {
    return *overlay_controller_;
}

FLASHMEM lv_obj_t* StandaloneOverlayAssembly::viewSelectorElement() const {
    return view_selector_ ? view_selector_->getElement() : nullptr;
}

FLASHMEM oc::type::ScopeID StandaloneOverlayAssembly::viewSelectorScope() const {
    return view_selector_scope_;
}

FLASHMEM void StandaloneOverlayAssembly::renderViewSelector(int selectedIndex, bool visible) {
    if (!view_selector_) return;
    if (!visible) {
        view_selector_->hide();
        return;
    }

    for (int i = 0; i < core::state::VIEW_SELECTOR_ITEM_COUNT && i < static_cast<int>(view_selector_rows_.size()); ++i) {
        const auto item = core::state::viewSelectorItemAt(i);
        view_selector_rows_[static_cast<std::size_t>(i)] = ms::ui::MenuRow{
            .label = core::state::viewSelectorItemLabel(item),
            .value = core::state::viewSelectorItemDescription(item),
            .kind = ms::ui::MenuRowKind::Folder,
            .enabled = true,
            .valueAutoScroll = true,
            .valueRole = ms::ui::MenuRowValueRole::Description,
        };
    }

    view_selector_->show();
    view_selector_->render({
        .title = "Select View",
        .meta = "Hold Back + Nav",
        .rows = view_selector_rows_.data(),
        .rowCount = core::state::VIEW_SELECTOR_ITEM_COUNT,
        .selectedIndex = selectedIndex,
        .dataRevision = 1,
    });
}

FLASHMEM void StandaloneOverlayAssembly::createOverlayController(
    oc::api::ButtonAPI& buttons,
    ActiveViewScopeProvider activeViewScopeProvider
) {
    overlay_controller_ = core::app::makeExtmemUnique<oc::context::OverlayManager<core::ui::OverlayType>>(
        core_state_.overlays,
        buttons
    );
    overlay_controller_->setActiveViewProvider(std::move(activeViewScopeProvider));
}

FLASHMEM void StandaloneOverlayAssembly::createViewSelectorOverlay(lv_obj_t* overlayRoot) {
    view_selector_ = core::app::makeExtmemUnique<ms::ui::MenuListView>(overlayRoot);
    view_selector_->hide();
    view_selector_scope_ = oc::ui::lvgl::scopeID(view_selector_->getElement());
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::VIEW_SELECTOR,
        view_selector_scope_,
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP)
    );
}

}  // namespace core::context::standalone
