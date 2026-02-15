#pragma once

/**
 * @file SequencerMacroPropertyHandler.hpp
 * @brief Map 8 macro encoders to the active sequencer step property
 */

#include <lvgl.h>

#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

class SequencerMacroPropertyHandler {
public:
    SequencerMacroPropertyHandler(
        core::state::CoreState& state,
        oc::api::EncoderAPI& encoders,
        lv_obj_t* sequencerViewScope
    );

    SequencerMacroPropertyHandler(const SequencerMacroPropertyHandler&) = delete;
    SequencerMacroPropertyHandler& operator=(const SequencerMacroPropertyHandler&) = delete;

private:
    void setupBindings();
    void handleTurn(uint8_t indexInPage, float normalized);
    void handleFocusedTurn(float normalized);
    void bumpRevision();

    core::state::CoreState& state_;
    oc::api::EncoderAPI& encoders_;
    lv_obj_t* scope_element_ = nullptr;
};

}  // namespace core::handler
