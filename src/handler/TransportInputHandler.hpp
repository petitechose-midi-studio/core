#pragma once

/**
 * @file TransportInputHandler.hpp
 * @brief Handles transport controls (tempo, play/stop)
 *
 * - NAV encoder: tempo +/- 1 BPM
 * - BOTTOM_CENTER button: toggle play
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"

namespace handler {

class TransportInputHandler {
public:
    TransportInputHandler(state::CoreState& coreState,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          lv_obj_t* scopeElement);

    ~TransportInputHandler() = default;

    TransportInputHandler(const TransportInputHandler&) = delete;
    TransportInputHandler& operator=(const TransportInputHandler&) = delete;

private:
    void setupBindings();
    void handleTempoChange(float delta);
    void handlePlayToggle();

    state::CoreState& coreState_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* scopeElement_;

    static constexpr float TEMPO_MIN = 20.0f;
    static constexpr float TEMPO_MAX = 300.0f;
};

}  // namespace handler
