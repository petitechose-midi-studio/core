#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class SequencerTrackSelectorHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
    };

    SequencerTrackSelectorHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId
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

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
};

}  // namespace core::handler
