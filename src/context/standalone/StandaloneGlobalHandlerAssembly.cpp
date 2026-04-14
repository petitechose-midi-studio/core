#include "context/standalone/StandaloneGlobalHandlerAssembly.hpp"

#include <memory>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>
#include "handler/transport/TransportHandler.hpp"
#include "handler/view/ViewSwitcherHandler.hpp"
#include "state/CoreState.hpp"

namespace core::context::standalone {

class StandaloneGlobalHandlerAssembly::Impl {
public:
    Impl(core::state::CoreState& state,
         oc::context::OverlayManager<core::ui::OverlayType>& overlays,
         lv_obj_t* viewSelectorElement,
         oc::api::EncoderAPI& encoders,
         oc::api::ButtonAPI& buttons,
         oc::type::ScopeID macroViewScope,
         oc::type::ScopeID sequencerViewScope) {
        OC_LOG_DEBUG("StandaloneGlobalHandlerAssembly: transport_handler");
        transport_handler_ = core::app::makeExtmemUnique<core::handler::TransportHandler>(
            core::handler::TransportHandler::StateRefs{state.statusBar},
            encoders,
            buttons,
            macroViewScope,
            core::handler::TransportHandler::ViewScopes{
                macroViewScope,
                sequencerViewScope,
            }
        );

        using OverlayCtx = ms::ui::OverlayBindingContext<core::ui::OverlayType>;
        OverlayCtx overlayCtx{overlays, nullptr, viewSelectorElement};
        OC_LOG_DEBUG("StandaloneGlobalHandlerAssembly: view_switcher_handler");
        view_switcher_handler_ = core::app::makeExtmemUnique<core::handler::ViewSwitcherHandler>(
            core::handler::ViewSwitcherHandler::StateRefs{
                state.overlays,
                state.activeView,
                state.viewSelector,
                state.sequencer.rangeSelection,
                state.sequencer.patternQuickControls,
                state.sequencer.stepPropertyInlineSelector,
                state.trackNavigation.selection,
                state.macroUi.pageSelection,
                state.sequencer.structureUi.pageSelection,
            },
            overlayCtx,
            encoders,
            buttons,
            core::handler::ViewSwitcherHandler::ViewScopes{
                macroViewScope,
                sequencerViewScope,
            }
        );
        OC_LOG_DEBUG("StandaloneGlobalHandlerAssembly: ready");
    }

private:
    core::app::ExtmemUniquePtr<core::handler::TransportHandler> transport_handler_;
    core::app::ExtmemUniquePtr<core::handler::ViewSwitcherHandler> view_switcher_handler_;
};

FLASHMEM StandaloneGlobalHandlerAssembly::StandaloneGlobalHandlerAssembly(
    core::state::CoreState& state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    lv_obj_t* viewSelectorElement,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID macroViewScope,
    oc::type::ScopeID sequencerViewScope
)
    : impl_(core::app::makeExtmemUnique<Impl>(
          state,
          overlays,
          viewSelectorElement,
          encoders,
          buttons,
          macroViewScope,
          sequencerViewScope
      )) {
}

FLASHMEM StandaloneGlobalHandlerAssembly::~StandaloneGlobalHandlerAssembly() = default;

}  // namespace core::context::standalone
