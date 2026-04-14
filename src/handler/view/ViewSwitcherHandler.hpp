#pragma once

/**
 * @file ViewSwitcherHandler.hpp
 * @brief Handles top-level view switching via LEFT_TOP selector overlay
 */

#include <array>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>
#include <ms/ui/OverlayBindingContext.hpp>

#include "state/ViewSelectorState.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace core::handler {

class ViewSwitcherHandler {
public:
    using OverlayCtx = ms::ui::OverlayBindingContext<core::ui::OverlayType>;
    static constexpr std::size_t VIEW_SCOPE_COUNT = static_cast<std::size_t>(core::ui::ViewType::COUNT);
    using ViewScopes = std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT>;
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::ViewSelectorState& viewSelector;
        core::state::sequencer::SequencerRangeSelectionState& rangeSelection;
        core::state::sequencer::SequencerPatternQuickControlsState& patternQuickControls;
        core::state::sequencer::SequencerStepPropertyInlineSelectorState&
            stepPropertyInlineSelector;
        core::state::StructureSelectionState& trackStructureSelection;
        core::state::StructureSelectionState& macroPageSelection;
        core::state::StructureSelectionState& sequencerPageSelection;
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
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::ViewSelectorState& view_selector_;
    core::state::sequencer::SequencerRangeSelectionState& range_selection_;
    core::state::sequencer::SequencerPatternQuickControlsState& pattern_quick_controls_;
    core::state::sequencer::SequencerStepPropertyInlineSelectorState&
        step_property_inline_selector_;
    core::state::StructureSelectionState& track_structure_selection_;
    core::state::StructureSelectionState& macro_page_selection_;
    core::state::StructureSelectionState& sequencer_page_selection_;
    OverlayCtx overlay_ctx_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    ViewScopes view_scopes_{};

    static constexpr int VIEW_COUNT = static_cast<int>(core::ui::ViewType::COUNT);
};

}  // namespace core::handler
