#pragma once

/**
 * @file ViewSwitcherHandler.hpp
 * @brief Handles top-level view switching via LEFT_TOP selector overlay
 */

#include <lvgl.h>

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <ms/ui/OverlayBindingContext.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"
#include "ui/ViewTypes.hpp"

namespace core::handler {

class ViewSwitcherHandler {
public:
    using OverlayCtx = ms::ui::OverlayBindingContext<core::ui::OverlayType>;
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<lv_obj_t*, VIEW_SCOPE_COUNT>;

    ViewSwitcherHandler(core::state::CoreState& state,
                        OverlayCtx overlayCtx,
                        oc::api::EncoderAPI& encoders,
                        oc::api::ButtonAPI& buttons,
                        ViewScopes viewScopes);

    ~ViewSwitcherHandler() = default;

    ViewSwitcherHandler(const ViewSwitcherHandler&) = delete;
    ViewSwitcherHandler& operator=(const ViewSwitcherHandler&) = delete;

private:
    void setupBindings();

    void openSelector();
    void navigate(float delta);
    void confirmSelection();
    void closeSelector();

    core::state::CoreState& state_;
    OverlayCtx overlay_ctx_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};

    static constexpr int VIEW_COUNT = static_cast<int>(core::ui::ViewType::COUNT);
};

}  // namespace core::handler
