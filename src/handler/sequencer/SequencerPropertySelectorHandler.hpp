#pragma once

/**
 * @file SequencerPropertySelectorHandler.hpp
 * @brief Input bindings for inline sequencer step-property selection
 */

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class SequencerPropertySelectorHandler {
public:
    using NowProvider = uint32_t (*)();

    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::TrackNavigationState& trackNavigation;
        SequencerHistoryDomainServices history;
    };

    SequencerPropertySelectorHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId,
        NowProvider nowProvider
    );

    SequencerPropertySelectorHandler(const SequencerPropertySelectorHandler&) = delete;
    SequencerPropertySelectorHandler& operator=(const SequencerPropertySelectorHandler&) = delete;

private:
    void setupBindings();

    void open();
    void closeApply();
    void closeCancel();

    void navigate(float delta);
    void setActiveVariationRange(float normalized);
    void configureOptForSelectedProperty();

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::TrackNavigationState& track_ui_;
    SequencerHistoryDomainServices history_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    NowProvider now_provider_ = nullptr;
    oc::note::sequencer::StepSequencerVariationRanges snapshot_variation_ranges_{};
};

}  // namespace core::handler
