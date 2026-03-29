#pragma once

/**
 * @file SequencerStepEditHandler.hpp
 * @brief Input bindings for the sequencer STEP EDIT overlay
 */

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class SequencerStepEditHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
    };

    SequencerStepEditHandler(
        StateRefs state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID sequencerViewScope,
        oc::type::ScopeID overlayScope
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

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlay_state_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
};

}  // namespace core::handler
