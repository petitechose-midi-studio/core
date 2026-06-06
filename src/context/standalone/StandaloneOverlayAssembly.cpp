#include "context/standalone/StandaloneOverlayAssembly.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include <config/App.hpp>
#include <ms/ui/widget/StringListSelector.hpp>
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

FLASHMEM ms::ui::StringListSelector& StandaloneOverlayAssembly::viewSelector() const {
    return *view_selector_;
}

FLASHMEM lv_obj_t* StandaloneOverlayAssembly::viewSelectorElement() const {
    return view_selector_->getElement();
}

FLASHMEM oc::type::ScopeID StandaloneOverlayAssembly::viewSelectorScope() const {
    return view_selector_scope_;
}

FLASHMEM void StandaloneOverlayAssembly::renderViewSelector(int selectedIndex, bool visible) {
    view_selector_->render({
        .items = core::state::VIEW_SELECTOR_ITEM_LABELS.data(),
        .itemCount = core::state::VIEW_SELECTOR_ITEM_LABELS.size(),
        .selectedIndex = selectedIndex,
        .visible = visible,
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
    view_selector_ = core::app::makeExtmemUnique<ms::ui::StringListSelector>(overlayRoot);
    view_selector_->setTitle("Select View");
    view_selector_scope_ = oc::ui::lvgl::scopeID(view_selector_->getElement());
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::VIEW_SELECTOR,
        view_selector_scope_,
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP)
    );
}

}  // namespace core::context::standalone
