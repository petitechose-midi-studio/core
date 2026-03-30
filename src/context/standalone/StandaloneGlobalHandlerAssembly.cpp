#include "context/standalone/StandaloneGlobalHandlerAssembly.hpp"

#include <memory>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

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
        transport_handler_ = std::make_unique<core::handler::TransportHandler>(
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
        view_switcher_handler_ = std::make_unique<core::handler::ViewSwitcherHandler>(
            core::handler::ViewSwitcherHandler::StateRefs{
                state.overlays,
                state.activeView,
                state.viewSelector,
                state.sequencer.rangeSelection,
                state.sequencerTracks.selector,
                state.sequencer.patternQuickControls,
                state.sequencer.stepPropertyInlineSelector,
            },
            overlayCtx,
            encoders,
            buttons,
            core::handler::ViewSwitcherHandler::ViewScopes{
                macroViewScope,
                sequencerViewScope,
            }
        );
    }

private:
    std::unique_ptr<core::handler::TransportHandler> transport_handler_;
    std::unique_ptr<core::handler::ViewSwitcherHandler> view_switcher_handler_;
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
    : impl_(std::make_unique<Impl>(
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
