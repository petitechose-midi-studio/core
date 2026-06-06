#pragma once

#include <array>
#include <functional>
#include <memory>

#include "app/ExtmemAllocator.hpp"
#include <lvgl.h>

#include <oc/type/Ids.hpp>
#include <ms/ui/widget/MenuListView.hpp>

#include "app/OverlayTypes.hpp"
#include "state/ViewSelectorItems.hpp"

namespace core::state {
struct CoreState;
}

namespace oc::api {
class ButtonAPI;
}  // namespace oc::api

namespace oc::context {
template <typename T>
class OverlayManager;
}  // namespace oc::context

namespace core::context::standalone {

/**
 * Owns standalone overlay controller setup and the view selector widget.
 *
 * It registers overlay cleanup against LVGL scopes and delegates active-view
 * scope lookup through the provider passed by the context.
 */
class StandaloneOverlayAssembly {
public:
    using ActiveViewScopeProvider = std::function<oc::type::ScopeID()>;

    StandaloneOverlayAssembly(core::state::CoreState& state,
                              oc::api::ButtonAPI& buttons,
                              lv_obj_t* overlayRoot,
                              ActiveViewScopeProvider activeViewScopeProvider);
    ~StandaloneOverlayAssembly();

    StandaloneOverlayAssembly(const StandaloneOverlayAssembly&) = delete;
    StandaloneOverlayAssembly& operator=(const StandaloneOverlayAssembly&) = delete;

    oc::context::OverlayManager<core::ui::OverlayType>& controller() const;
    lv_obj_t* viewSelectorElement() const;
    oc::type::ScopeID viewSelectorScope() const;
    void renderViewSelector(int selectedIndex, bool visible);

private:
    void createOverlayController(oc::api::ButtonAPI& buttons,
                                 ActiveViewScopeProvider activeViewScopeProvider);
    void createViewSelectorOverlay(lv_obj_t* overlayRoot);

    core::state::CoreState& core_state_;
    core::app::ExtmemUniquePtr<oc::context::OverlayManager<core::ui::OverlayType>> overlay_controller_;
    core::app::ExtmemUniquePtr<ms::ui::MenuListView> view_selector_;
    std::array<ms::ui::MenuRow, core::state::VIEW_SELECTOR_ITEM_COUNT> view_selector_rows_{};
    oc::type::ScopeID view_selector_scope_ = 0;
};

}  // namespace core::context::standalone
