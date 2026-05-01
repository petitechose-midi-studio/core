#pragma once

#include <memory>

#include "app/ExtmemAllocator.hpp"
#include <lvgl.h>

#include <oc/type/Ids.hpp>

#include "app/OverlayTypes.hpp"

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

namespace core::validation::ux {
class SemanticUxSurfaceRegistry;
}

namespace core::context::standalone {

/**
 * Owns handlers that are global to the standalone context.
 *
 * Transport and view-switcher bindings live here because they span macro and
 * sequencer view scopes. Feature-specific modal bindings stay in feature
 * modules.
 */
class StandaloneGlobalHandlerAssembly {
public:
    StandaloneGlobalHandlerAssembly(core::state::CoreState& state,
                                    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                    lv_obj_t* viewSelectorElement,
                                    oc::api::EncoderAPI& encoders,
                                    oc::api::ButtonAPI& buttons,
                                    oc::type::ScopeID macroViewScope,
                                    oc::type::ScopeID sequencerViewScope
#if defined(MS_UX_RECORDER)
                                    ,
                                    core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    );
    ~StandaloneGlobalHandlerAssembly();

    StandaloneGlobalHandlerAssembly(const StandaloneGlobalHandlerAssembly&) = delete;
    StandaloneGlobalHandlerAssembly& operator=(const StandaloneGlobalHandlerAssembly&) = delete;

private:
    class Impl;
    core::app::ExtmemUniquePtr<Impl> impl_;
};

}  // namespace core::context::standalone
