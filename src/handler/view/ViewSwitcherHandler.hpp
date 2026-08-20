#pragma once

/**
 * @file ViewSwitcherHandler.hpp
 * @brief Handles top-level view switching via LEFT_TOP selector overlay
 */

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class ViewSwitcherHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;

    ViewSwitcherHandler(
        core::state::CoreState& state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        ViewScopes viewScopes,
        oc::type::ScopeID viewSelectorScope
    );

    ~ViewSwitcherHandler() = default;

    ViewSwitcherHandler(const ViewSwitcherHandler&) = delete;
    ViewSwitcherHandler& operator=(const ViewSwitcherHandler&) = delete;

private:
    void setupBindings();
    bool canOpenSelector() const;

    bool beginSelectorPress();
    [[nodiscard]] bool openSelector();
    void navigate(float delta);
    void confirmSelection();
    void closeSelector();
    void undoProjectHistory();
    void redoProjectHistory();

    core::state::CoreState& core_state_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};
    oc::type::ScopeID view_selector_scope_ = 0;
};

}  // namespace core::handler
