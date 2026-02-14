#pragma once

/**
 * @file SequencerPropertySelectorHandler.hpp
 * @brief Input bindings for the sequencer PROPERTY SELECTOR overlay
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class SequencerPropertySelectorHandler {
public:
    SequencerPropertySelectorHandler(
        core::state::CoreState& state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* sequencerViewScope,
        lv_obj_t* overlayScope
    );

    SequencerPropertySelectorHandler(const SequencerPropertySelectorHandler&) = delete;
    SequencerPropertySelectorHandler& operator=(const SequencerPropertySelectorHandler&) = delete;

private:
    void setupBindings();

    void open();
    void closeApply();
    void closeCancel();

    void navigate(float delta);
    void applySelection();

    core::state::CoreState& state_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* sequencer_view_scope_ = nullptr;
    lv_obj_t* overlay_scope_ = nullptr;
};

}  // namespace core::handler
