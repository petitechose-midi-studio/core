#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

class SequencerTrackSelectorHandler {
public:
    SequencerTrackSelectorHandler(
        core::state::CoreState& state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* sequencerViewScope
    );

    SequencerTrackSelectorHandler(const SequencerTrackSelectorHandler&) = delete;
    SequencerTrackSelectorHandler& operator=(const SequencerTrackSelectorHandler&) = delete;

private:
    void setupBindings();
    void open();
    void navigate(float delta);
    void toggleSelectedTrackEnabled();
    void closeApplyIfReleased();
    void closeCancel();

    core::state::CoreState& state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* sequencer_view_scope_ = nullptr;
};

}  // namespace core::handler
