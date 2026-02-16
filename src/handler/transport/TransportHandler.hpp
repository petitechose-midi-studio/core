#pragma once

/**
 * @file TransportHandler.hpp
 * @brief Handles transport controls (tempo, play/stop)
 *
 * - NAV encoder: tempo +/- 1 BPM
 * - BOTTOM_CENTER button: toggle play (transport scope)
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include <config/InputIDs.hpp>
#include "state/CoreState.hpp"

namespace core::handler {

class TransportHandler {
public:
    TransportHandler(core::state::CoreState& coreState,
                           oc::api::EncoderAPI& encoders,
                           oc::api::ButtonAPI& buttons,
                           lv_obj_t* tempoScopeElement,
                           lv_obj_t* transportScopeElement);

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
    lv_obj_t* transport_scope_element_;

    static constexpr float TEMPO_MIN = 20.0f;
    static constexpr float TEMPO_MAX = 300.0f;
};

}  // namespace core::handler
