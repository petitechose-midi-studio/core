#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/type/Ids.hpp>

#include "ui/OverlayTypes.hpp"

namespace core::state {
struct CoreState;
}

namespace oc::api {
class ButtonAPI;
class EncoderAPI;
}  // namespace oc::api

namespace oc::context {
template <typename T>
class OverlayManager;
}  // namespace oc::context

namespace core::context::standalone {

class StandaloneGlobalHandlerAssembly {
public:
    StandaloneGlobalHandlerAssembly(core::state::CoreState& state,
                                    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                    lv_obj_t* viewSelectorElement,
                                    oc::api::EncoderAPI& encoders,
                                    oc::api::ButtonAPI& buttons,
                                    oc::type::ScopeID macroViewScope,
                                    oc::type::ScopeID sequencerViewScope);
    ~StandaloneGlobalHandlerAssembly();

    StandaloneGlobalHandlerAssembly(const StandaloneGlobalHandlerAssembly&) = delete;
    StandaloneGlobalHandlerAssembly& operator=(const StandaloneGlobalHandlerAssembly&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace core::context::standalone
