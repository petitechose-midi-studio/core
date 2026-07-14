#pragma once

/**
 * @file SequencerPropertySelectorHandler.hpp
 * @brief Input bindings for inline sequencer step-property selection
 */

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class SequencerCcLaneWorkflow;

class SequencerPropertySelectorHandler {
public:
    using NowProvider = uint32_t (*)();

    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        SequencerHistoryDomainServices history;
    };

    SequencerPropertySelectorHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId,
        NowProvider nowProvider,
        SequencerCcLaneWorkflow* ccLaneWorkflow = nullptr,
        oc::context::OverlayManager<core::ui::OverlayType>* overlayManager = nullptr
    );

    SequencerPropertySelectorHandler(const SequencerPropertySelectorHandler&) = delete;
    SequencerPropertySelectorHandler& operator=(const SequencerPropertySelectorHandler&) = delete;

    /** Open the shared property grammar focused on CC lanes from Lane Grid. */
    void openCcLaneShortcut();

private:
    void setupBindings();

    void open();
    void closeApply();
    void closeCancel();

    void navigate(float delta);
    void setActiveVariationRange(float normalized);
    void configureOptForSelectedProperty();
    void applySelectedCcLaneProperty(int selectedIndex);

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::TrackNavigationState& track_ui_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    SequencerHistoryDomainServices history_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    NowProvider now_provider_ = nullptr;
    SequencerCcLaneWorkflow* cc_lane_workflow_ = nullptr;
    oc::context::OverlayManager<core::ui::OverlayType>* overlay_manager_ = nullptr;
    oc::note::sequencer::StepSequencerVariationRanges snapshot_variation_ranges_{};
    core::state::sequencer::SequencerHistoryPatternSnapshot history_snapshot_{};
    bool history_snapshot_valid_ = false;
    bool restore_cc_lane_on_cancel_ = false;
};

}  // namespace core::handler
