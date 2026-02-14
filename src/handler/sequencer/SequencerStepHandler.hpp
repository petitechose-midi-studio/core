#pragma once

/**
 * @file SequencerStepHandler.hpp
 * @brief Standalone sequencer step editing bindings
 */

#include <cstdint>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

/**
 * v0 bindings (sequencer view scope):
 * - MACRO_1..MACRO_8 release: toggle step in current page
 * - BOTTOM_LEFT / BOTTOM_RIGHT release: page switch
 * - NAV turn: move focus across steps (wrap)
 * - NAV release: toggle focused step
 */
class SequencerStepHandler {
public:
    SequencerStepHandler(core::state::CoreState& state,
                        oc::api::EncoderAPI& encoders,
                        oc::api::ButtonAPI& buttons,
                        lv_obj_t* scopeElement);

    ~SequencerStepHandler() = default;

    SequencerStepHandler(const SequencerStepHandler&) = delete;
    SequencerStepHandler& operator=(const SequencerStepHandler&) = delete;
    SequencerStepHandler(SequencerStepHandler&&) = delete;
    SequencerStepHandler& operator=(SequencerStepHandler&&) = delete;

private:
    void setupBindings();

    void toggleStep(uint8_t indexInPage);
    void toggleFocusedStep();
    void moveFocus(float delta);
    void prevPage();
    void nextPage();

    core::state::CoreState& state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* scope_element_ = nullptr;
};

}  // namespace core::handler
