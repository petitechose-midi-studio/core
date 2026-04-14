#pragma once

/**
 * @file SequencerMacroPropertyHandler.hpp
 * @brief Map 8 macro encoders to the active sequencer step property
 */

#include <cstdint>

#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class SequencerMacroPropertyHandler {
public:
    using NowProvider = uint32_t (*)();

    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::TrackNavigationState& trackNavigation;
        core::state::sequencer::SequencerTrackBankState& tracks;
    };

    SequencerMacroPropertyHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::type::ScopeID scopeId,
        NowProvider nowProvider
    );

    SequencerMacroPropertyHandler(const SequencerMacroPropertyHandler&) = delete;
    SequencerMacroPropertyHandler& operator=(const SequencerMacroPropertyHandler&) = delete;

private:
    void setupBindings();
    void handleTurn(uint8_t indexInPage, float normalized);
    void handleFocusedTurn(float normalized);

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::TrackNavigationState& track_ui_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::api::EncoderAPI& encoders_;
    oc::type::ScopeID scope_id_ = 0;
    NowProvider now_provider_ = nullptr;
};

}  // namespace core::handler
