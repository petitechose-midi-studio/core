#pragma once

/**
 * @file SequencerMacroPropertyHandler.hpp
 * @brief Map 8 macro encoders to the active sequencer step property
 */

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StructureNavigationState.hpp"
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
        core::state::sequencer::SequencerTrackBankState& trackBank;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        SequencerHistoryDomainServices history;
    };

    SequencerMacroPropertyHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId,
        NowProvider nowProvider
    );

    SequencerMacroPropertyHandler(const SequencerMacroPropertyHandler&) = delete;
    SequencerMacroPropertyHandler& operator=(const SequencerMacroPropertyHandler&) = delete;

private:
    void setupBindings();
    void handleTurn(uint8_t indexInPage, float normalized);
    void handleFocusedStepTurn(float normalized);

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& track_bank_;
    core::state::TrackNavigationState& track_ui_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    SequencerHistoryDomainServices history_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    NowProvider now_provider_ = nullptr;
};

}  // namespace core::handler
