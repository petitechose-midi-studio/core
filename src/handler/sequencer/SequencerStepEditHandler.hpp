#pragma once

/**
 * @file SequencerStepEditHandler.hpp
 * @brief Input bindings for the sequencer STEP EDIT overlay
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class SequencerStepEditHandler {
public:
    SequencerStepEditHandler(
        core::state::CoreState& state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* sequencerViewScope,
        lv_obj_t* overlayScope
    );

    // Non-copyable, non-movable
    SequencerStepEditHandler(const SequencerStepEditHandler&) = delete;
    SequencerStepEditHandler& operator=(const SequencerStepEditHandler&) = delete;
    SequencerStepEditHandler(SequencerStepEditHandler&&) = delete;
    SequencerStepEditHandler& operator=(SequencerStepEditHandler&&) = delete;

private:
    void setupBindings();

    void openForMacroInPage(uint8_t indexInPage);
    void closeApply();
    void closeCancel();

    void moveFocus(float delta);
    void setFocusedValue(float normalized);
    void configureOptForFocusedRow();
    void maybeCloseApplyFromMacro(uint8_t indexInPage);

    // Long-press opens while still pressed; ignore the release that follows.
    bool ignore_open_release_ = false;
    uint8_t ignore_open_macro_index_in_page_ = 0;

    core::state::CoreState& state_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* sequencer_view_scope_ = nullptr;
    lv_obj_t* overlay_scope_ = nullptr;
};

}  // namespace core::handler
