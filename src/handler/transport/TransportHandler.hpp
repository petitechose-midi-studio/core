#pragma once

/**
 * @file TransportHandler.hpp
 * @brief Handles transport controls (tempo, play/stop)
 *
 * - NAV encoder: tempo +/- 1 BPM
 * - BOTTOM_CENTER button: toggle play (active top-level view scopes)
 */

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

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
                     oc::api::EncoderAPI& encoders,
                     oc::api::ButtonAPI& buttons,
                     oc::type::ScopeID tempoScope,
                     ViewScopes playToggleScopes);

    ~TransportHandler() = default;

    TransportHandler(const TransportHandler&) = delete;
    TransportHandler& operator=(const TransportHandler&) = delete;

private:
    void setupBindings();
    void handleTempoChange(float delta);
    void handlePlayToggle();

    core::state::StatusBarState& status_bar_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID tempo_scope_id_ = 0;
    ViewScopes play_toggle_scopes_{};

    static constexpr float TEMPO_MIN = 20.0f;
    static constexpr float TEMPO_MAX = 300.0f;
};

}  // namespace core::handler
