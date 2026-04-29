#pragma once

/**
 * @file SequencerPropertySelectorHandler.hpp
 * @brief Input bindings for inline sequencer step-property selection
 */

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class SequencerPropertySelectorHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::TrackNavigationState& trackNavigation;
    };

    SequencerPropertySelectorHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId
    );

    SequencerPropertySelectorHandler(const SequencerPropertySelectorHandler&) = delete;
    SequencerPropertySelectorHandler& operator=(const SequencerPropertySelectorHandler&) = delete;

private:
    void setupBindings();

    void open();
    void closeApply();
    void closeCancel();

    void navigate(float delta);
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::TrackNavigationState& track_ui_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
};

}  // namespace core::handler
