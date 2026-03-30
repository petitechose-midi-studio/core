#pragma once

#include <functional>
#include <memory>

#include <lvgl.h>

#include <oc/type/Ids.hpp>

#include "ui/OverlayTypes.hpp"

namespace core::state {
struct CoreState;
}

namespace ms::ui {
class StringListSelector;
}  // namespace ms::ui

namespace oc::api {
class ButtonAPI;
}  // namespace oc::api

namespace oc::context {
template <typename T>
class OverlayManager;
}  // namespace oc::context

namespace core::context::standalone {

class StandaloneOverlayAssembly {
public:
    using ActiveViewScopeProvider = std::function<oc::type::ScopeID()>;

    StandaloneOverlayAssembly(core::state::CoreState& state,
                              oc::api::ButtonAPI& buttons,
                              lv_obj_t* mainZone,
                              ActiveViewScopeProvider activeViewScopeProvider);
    ~StandaloneOverlayAssembly();

    StandaloneOverlayAssembly(const StandaloneOverlayAssembly&) = delete;
    StandaloneOverlayAssembly& operator=(const StandaloneOverlayAssembly&) = delete;

    oc::context::OverlayManager<core::ui::OverlayType>& controller() const;
    ms::ui::StringListSelector& viewSelector() const;
    lv_obj_t* viewSelectorElement() const;
    oc::type::ScopeID viewSelectorScope() const;
    void renderViewSelector(int selectedIndex, bool visible);

private:
    void createOverlayController(oc::api::ButtonAPI& buttons,
                                 ActiveViewScopeProvider activeViewScopeProvider);
    void createViewSelectorOverlay(lv_obj_t* mainZone);

    core::state::CoreState& core_state_;
    std::unique_ptr<oc::context::OverlayManager<core::ui::OverlayType>> overlay_controller_;
    std::unique_ptr<ms::ui::StringListSelector> view_selector_;
    oc::type::ScopeID view_selector_scope_ = 0;
};

}  // namespace core::context::standalone
