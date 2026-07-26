#pragma once

/**
 * @file ViewSwitcherHandler.hpp
 * @brief Handles top-level view switching via LEFT_TOP selector overlay
 */

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "state/ViewSelectorState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class ViewSwitcherHandler {
public:
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;
    struct StateRefs {
        core::state::CoreState& coreState;
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::ViewSelectorState& viewSelector;
        core::state::sequencer::SequencerPatternQuickControlsState& patternQuickControls;
        core::state::sequencer::SequencerStepPropertyInlineSelectorState&
            stepPropertyInlineSelector;
        core::state::sequencer::SequencerCcLaneUiState& ccLaneUi;
        core::state::sequencer::SequencerStepSelectionState& sequencerStepSelection;
        core::state::project::ProjectNavigationState& projectNavigation;
    };

    ViewSwitcherHandler(StateRefs state,
                        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                        oc::api::EncoderAPI& encoders,
                        oc::api::ButtonAPI& buttons,
                        ViewScopes viewScopes,
                        oc::type::ScopeID viewSelectorScope);

    ~ViewSwitcherHandler() = default;

    ViewSwitcherHandler(const ViewSwitcherHandler&) = delete;
    ViewSwitcherHandler& operator=(const ViewSwitcherHandler&) = delete;

private:
    void setupBindings();
    bool canOpenSelector() const;

    bool beginSelectorPress();
    [[nodiscard]] bool openSelector();
    void navigate(float delta);
    void confirmSelection();
    void closeSelector();
    void undoProjectHistory();
    void redoProjectHistory();

    core::state::CoreState& core_state_;
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_state_;
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::ViewSelectorState& view_selector_;
    core::state::sequencer::SequencerPatternQuickControlsState& pattern_quick_controls_;
    core::state::sequencer::SequencerStepPropertyInlineSelectorState&
        step_property_inline_selector_;
    core::state::sequencer::SequencerCcLaneUiState& cc_lane_ui_;
    core::state::sequencer::SequencerStepSelectionState& sequencer_step_selection_;
    core::state::project::ProjectNavigationState& project_navigation_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};
    oc::type::ScopeID view_selector_scope_ = 0;

};

}  // namespace core::handler
