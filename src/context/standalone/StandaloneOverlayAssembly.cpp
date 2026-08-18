#include "context/standalone/StandaloneOverlayAssembly.hpp"

#include <array>

#include <config/PlatformCompat.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include "context/standalone/OverlayPresentationRegistry.hpp"
#include "state/CoreState.hpp"
#include "state/ViewSelectorItems.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/interaction/SelectorPresentationPolicy.hpp"

namespace core::context::standalone {

namespace {

constexpr std::array<const char*, core::state::VIEW_SELECTOR_ITEM_COUNT>
    VIEW_SELECTOR_ICONS = {
        ::standalone::icons::VIEW_MACROS,
        ::standalone::icons::VIEW_SEQUENCER,
        ::standalone::icons::MODULATION,
        ::standalone::icons::VIEW_PROJECT,
        ::standalone::icons::VIEW_DEVICE,
    };

}  // namespace

FLASHMEM StandaloneOverlayAssembly::StandaloneOverlayAssembly(
    core::state::CoreState& state,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* overlayRoot,
    ActiveViewScopeProvider activeViewScopeProvider
)
    : core_state_(state) {
    presentation_registry_ =
        core::app::makeExtmemUnique<OverlayPresentationRegistry>(overlayRoot);
    if (!presentation_registry_ || !presentation_registry_->valid()) return;
    if (!createOverlayController(buttons, std::move(activeViewScopeProvider))) return;
    if (!createViewSelectorOverlay(overlayRoot)) return;
    valid_ = true;
}

FLASHMEM StandaloneOverlayAssembly::~StandaloneOverlayAssembly() = default;

FLASHMEM oc::context::OverlayManager<core::ui::OverlayType>&
StandaloneOverlayAssembly::controller() const {
    return *overlay_controller_;
}

FLASHMEM OverlayPresentationRegistry&
StandaloneOverlayAssembly::presentationRegistry() const {
    return *presentation_registry_;
}

FLASHMEM lv_obj_t* StandaloneOverlayAssembly::viewSelectorElement() const {
    return view_selector_ ? view_selector_->getElement() : nullptr;
}

FLASHMEM oc::type::ScopeID StandaloneOverlayAssembly::viewSelectorScope() const {
    return view_selector_scope_;
}

FLASHMEM void StandaloneOverlayAssembly::renderViewSelector(int selectedIndex, bool visible) {
    if (!valid_ || !view_selector_) return;
    if (!visible) {
        view_selector_->render({.visible = false});
        return;
    }

    auto props = core::ui::interaction::decisionSelectorProps(
        "Views",
        "",
        core::state::VIEW_SELECTOR_ITEM_LABELS.data(),
        core::state::VIEW_SELECTOR_ITEM_COUNT,
        selectedIndex,
        1U
    );
    props.icons = VIEW_SELECTOR_ICONS.data();
    props.iconFont = standalone_fonts.icons_16;
    view_selector_->render(props);
}

FLASHMEM bool StandaloneOverlayAssembly::createOverlayController(
    oc::api::ButtonAPI& buttons,
    ActiveViewScopeProvider activeViewScopeProvider
) {
    overlay_controller_ = core::app::makeExtmemUnique<oc::context::OverlayManager<core::ui::OverlayType>>(
        core_state_.overlays,
        buttons
    );
    if (!overlay_controller_) return false;
    overlay_controller_->setPresentationCallback(
        presentation_registry_.get(),
        &OverlayPresentationRegistry::onPresentationChanged
    );
    overlay_controller_->setActiveViewProvider(std::move(activeViewScopeProvider));
    return true;
}

FLASHMEM bool StandaloneOverlayAssembly::createViewSelectorOverlay(lv_obj_t* overlayRoot) {
    view_selector_ = core::app::makeExtmemUnique<
        ms::ui::VirtualListSelectorOverlay>(overlayRoot);
    if (!view_selector_ || !view_selector_->getElement()) return false;
    view_selector_->render({.visible = false});
    view_selector_scope_ = oc::ui::lvgl::scopeID(view_selector_->getElement());
    return registerOverlaySurface(
        *overlay_controller_,
        *presentation_registry_,
        core::ui::OverlayType::VIEW_SELECTOR,
        view_selector_->getElement(),
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP)
    );
}

}  // namespace core::context::standalone
