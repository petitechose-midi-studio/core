#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

class SequencerPatternQuickControlsHandler {
public:
    SequencerPatternQuickControlsHandler(
        core::state::CoreState& state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* sequencerViewScope
    );

    SequencerPatternQuickControlsHandler(const SequencerPatternQuickControlsHandler&) = delete;
    SequencerPatternQuickControlsHandler& operator=(const SequencerPatternQuickControlsHandler&) = delete;

private:
    void setupBindings();
    void open();
    void closeApply();
    void closeCancel();
    void navigate(float delta);
    void setFocusedValue(float normalized);
    void configureOptForFocusedItem();
    void clampFocusToLength();

    core::state::CoreState& state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* sequencer_view_scope_ = nullptr;
};

}  // namespace core::handler
