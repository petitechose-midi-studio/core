#include "context/standalone/StandaloneGlobalHandlerAssembly.hpp"

#include <memory>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/PlatformCompat.hpp>
#include "handler/transport/TransportHandler.hpp"
#include "handler/view/ViewSwitcherHandler.hpp"
#include "state/CoreState.hpp"
#if defined(MS_UX_RECORDER)
#include "context/standalone/ux/StandaloneUxSurfaces.hpp"
#endif

namespace core::context::standalone {

class StandaloneGlobalHandlerAssembly::Impl {
public:
    Impl(core::state::CoreState& state,
         oc::context::OverlayManager<core::ui::OverlayType>& overlays,
         lv_obj_t* viewSelectorElement,
         oc::api::EncoderAPI& encoders,
         oc::api::ButtonAPI& buttons,
         oc::type::ScopeID macroViewScope,
         oc::type::ScopeID sequencerViewScope,
         oc::type::ScopeID projectViewScope,
         oc::type::ScopeID deviceSettingsViewScope
#if defined(MS_UX_RECORDER)
         ,
         core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    )
#if defined(MS_UX_RECORDER)
        : view_selector_ux_surface_(
              state.activeView,
              state.viewSelector,
              state.projectHistory
          ),
          transport_ux_surface_(state.statusBar)
#endif
    {
        if (!viewSelectorElement) return;
#if defined(MS_UX_RECORDER)
        if (uxRegistry &&
            (!uxRegistry->add(
                view_selector_ux_surface_,
                core::context::standalone::ux::priority::VIEW_SELECTOR
            ) ||
             !uxRegistry->add(
                transport_ux_surface_,
                core::context::standalone::ux::priority::TRANSPORT
            ))) return;
#endif

        transport_handler_ = core::app::makeExtmemUnique<core::handler::TransportHandler>(
            core::handler::TransportHandler::StateRefs{state.statusBar},
            buttons
        );
        if (!transport_handler_) return;

        const auto viewSelectorScope = oc::ui::lvgl::scopeID(viewSelectorElement);
        if (viewSelectorScope == 0) return;
        view_switcher_handler_ = core::app::makeExtmemUnique<core::handler::ViewSwitcherHandler>(
            state,
            overlays,
            encoders,
            buttons,
            core::handler::ViewSwitcherHandler::ViewScopes{
                macroViewScope,
                sequencerViewScope,
                projectViewScope,
                deviceSettingsViewScope,
                projectViewScope,
            },
            viewSelectorScope
        );
        if (!view_switcher_handler_) return;
        valid_ = true;
    }

    [[nodiscard]] bool valid() const { return valid_; }

private:
#if defined(MS_UX_RECORDER)
    core::context::standalone::ux::ViewSelectorUxSurface view_selector_ux_surface_;
    core::context::standalone::ux::TransportUxSurface transport_ux_surface_;
#endif

    core::app::ExtmemUniquePtr<core::handler::TransportHandler> transport_handler_;
    core::app::ExtmemUniquePtr<core::handler::ViewSwitcherHandler> view_switcher_handler_;
    bool valid_ = false;
};

FLASHMEM StandaloneGlobalHandlerAssembly::StandaloneGlobalHandlerAssembly(
    core::state::CoreState& state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    lv_obj_t* viewSelectorElement,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID macroViewScope,
    oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID projectViewScope,
    oc::type::ScopeID deviceSettingsViewScope
#if defined(MS_UX_RECORDER)
    ,
    core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
)
    : impl_(core::app::makeExtmemUnique<Impl>(
          state,
          overlays,
          viewSelectorElement,
          encoders,
          buttons,
          macroViewScope,
          sequencerViewScope,
          projectViewScope,
          deviceSettingsViewScope
#if defined(MS_UX_RECORDER)
          ,
          uxRegistry
#endif
      )) {
}

FLASHMEM StandaloneGlobalHandlerAssembly::~StandaloneGlobalHandlerAssembly() = default;

FLASHMEM bool StandaloneGlobalHandlerAssembly::valid() const {
    return impl_ && impl_->valid();
}

}  // namespace core::context::standalone
