#pragma once

/**
 * @file SequencerPatternConfigHandler.hpp
 * @brief Input bindings for the sequencer PATTERN CONFIG overlay
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class SequencerPatternConfigHandler {
public:
    SequencerPatternConfigHandler(
        core::state::CoreState& state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* sequencerViewScope,
        lv_obj_t* overlayScope
    );

    // Non-copyable, non-movable
    SequencerPatternConfigHandler(const SequencerPatternConfigHandler&) = delete;
    SequencerPatternConfigHandler& operator=(const SequencerPatternConfigHandler&) = delete;
    SequencerPatternConfigHandler(SequencerPatternConfigHandler&&) = delete;
    SequencerPatternConfigHandler& operator=(SequencerPatternConfigHandler&&) = delete;

private:
    void setupBindings();

    void open();
    void closeApply();
    void closeCancel();

    void moveFocus(float delta);
    void adjustValue(float delta);
    void clampFocusToLength();

    core::state::CoreState& state_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* sequencer_view_scope_ = nullptr;
    lv_obj_t* overlay_scope_ = nullptr;
};

}  // namespace core::handler
