#pragma once

/**
 * @file ViewSwitcherHandler.hpp
 * @brief Handles top-level view switching via LEFT_TOP selector overlay
 */

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <ms/ui/OverlayBindingContext.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"
#include "ui/ViewTypes.hpp"

namespace core::handler {

class ViewSwitcherHandler {
public:
    using OverlayCtx = ms::ui::OverlayBindingContext<core::ui::OverlayType>;
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType>& activeView;
        core::state::ViewSelectorState& viewSelector;
        core::state::sequencer::SequencerRangeSelectionState& rangeSelection;
        core::state::sequencer::SequencerTrackSelectorState& trackSelector;
        core::state::sequencer::SequencerPatternQuickControlsState& patternQuickControls;
        core::state::sequencer::SequencerStepPropertyInlineSelectorState&
            stepPropertyInlineSelector;
    };

    ViewSwitcherHandler(StateRefs state,
                        OverlayCtx overlayCtx,
                        oc::api::EncoderAPI& encoders,
                        oc::api::ButtonAPI& buttons,
                        ViewScopes viewScopes);

    ~ViewSwitcherHandler() = default;

    ViewSwitcherHandler(const ViewSwitcherHandler&) = delete;
    ViewSwitcherHandler& operator=(const ViewSwitcherHandler&) = delete;

private:
    void setupBindings();
    bool canOpenSelector() const;

    void openSelector();
    void navigate(float delta);
    void confirmSelection();
    void closeSelector();

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_state_;
    oc::state::Signal<core::ui::ViewType>& active_view_;
    core::state::ViewSelectorState& view_selector_;
    core::state::sequencer::SequencerRangeSelectionState& range_selection_;
    core::state::sequencer::SequencerTrackSelectorState& track_selector_;
    core::state::sequencer::SequencerPatternQuickControlsState& pattern_quick_controls_;
    core::state::sequencer::SequencerStepPropertyInlineSelectorState&
        step_property_inline_selector_;
    OverlayCtx overlay_ctx_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};

    static constexpr int VIEW_COUNT = static_cast<int>(core::ui::ViewType::COUNT);
};

}  // namespace core::handler
