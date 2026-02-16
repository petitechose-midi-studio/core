#pragma once

/**
 * @file TransportHandler.hpp
 * @brief Handles transport controls (tempo, play/stop)
 *
 * - NAV encoder: tempo +/- 1 BPM
 * - BOTTOM_CENTER button: toggle play (active top-level view scopes)
 */

#include <lvgl.h>

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include <config/InputIDs.hpp>
#include "state/CoreState.hpp"
#include "ui/ViewTypes.hpp"

namespace core::handler {

class TransportHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<lv_obj_t*, VIEW_SCOPE_COUNT>;

    TransportHandler(core::state::CoreState& coreState,
                            oc::api::EncoderAPI& encoders,
                            oc::api::ButtonAPI& buttons,
                            lv_obj_t* tempoScopeElement,
                            ViewScopes playToggleScopes);

    ~TransportHandler() = default;

    TransportHandler(const TransportHandler&) = delete;
    TransportHandler& operator=(const TransportHandler&) = delete;

private:
    void setupBindings();
    void handleTempoChange(float delta);
    void handlePlayToggle();

    core::state::CoreState& core_state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* tempo_scope_element_;
    ViewScopes play_toggle_scopes_{};

    static constexpr float TEMPO_MIN = 20.0f;
    static constexpr float TEMPO_MAX = 300.0f;
};

}  // namespace core::handler
