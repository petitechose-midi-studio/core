#pragma once

/**
 * @file TransportHandler.hpp
 * @brief Handles transport play/stop controls
 *
 * - BOTTOM_CENTER button: toggle play (active top-level view scopes)
 */

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>

#include <config/InputIDs.hpp>
#include "state/StatusBarState.hpp"
#include "app/ViewTypes.hpp"

namespace core::handler {

class TransportHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;
    struct StateRefs {
        core::state::StatusBarState& statusBar;
    };

    TransportHandler(StateRefs state,
                     oc::api::ButtonAPI& buttons,
                     ViewScopes playToggleScopes);

    ~TransportHandler() = default;

    TransportHandler(const TransportHandler&) = delete;
    TransportHandler& operator=(const TransportHandler&) = delete;

private:
    void setupBindings();
    void handlePlayToggle();

    core::state::StatusBarState& status_bar_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes play_toggle_scopes_{};
};

}  // namespace core::handler
