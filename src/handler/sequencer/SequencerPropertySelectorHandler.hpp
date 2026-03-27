#pragma once

/**
 * @file SequencerPropertySelectorHandler.hpp
 * @brief Input bindings for inline sequencer step-property selection
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include "state/CoreState.hpp"

namespace core::handler {

class SequencerPropertySelectorHandler {
public:
    SequencerPropertySelectorHandler(
        core::state::CoreState& state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* sequencerViewScope
    );

    SequencerPropertySelectorHandler(const SequencerPropertySelectorHandler&) = delete;
    SequencerPropertySelectorHandler& operator=(const SequencerPropertySelectorHandler&) = delete;

private:
    void setupBindings();

    void open();
    void closeApply();
    void closeCancel();

    void navigate(float delta);
    core::state::CoreState& state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* sequencer_view_scope_ = nullptr;
};

}  // namespace core::handler
