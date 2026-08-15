#pragma once

#include <array>

#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>
#include <oc/state/StaticSignalWatcher.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/MacroState.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace oc::api {
class EncoderAPI;
}

namespace core::context::standalone {

/**
 * Keeps physical encoder configuration/positions aligned with sequencer edit
 * state.
 *
 * The coordinator listens to view, overlay, page, length, focused-step, and edit
 * mode signals. It only updates encoder API state; it does not mutate pattern
 * data.
 */
class SequencerEncoderSyncCoordinator {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& trackBank;
    };

    SequencerEncoderSyncCoordinator(StateRefs state, oc::api::EncoderAPI& encoders);

    [[nodiscard]] bool bind();
    void reset();
    void syncNow();

private:
    void ensureMacroEncoderConfig(
        const core::handler::sequencer::input_utils::StepPropertyEncoderConfig& config
    );
    void ensureOptEncoderConfig(
        const core::handler::sequencer::input_utils::StepPropertyEncoderConfig& config
    );
    void syncMacroEncoderValues(
        uint8_t page,
        core::state::sequencer::StepProperty property
    );
    void syncMacroLocalVariationValues(
        uint8_t page,
        core::state::sequencer::StepProperty property
    );
    void syncMacroStateValues(uint8_t page);
    void invalidateOptEncoderCache();
    void syncOptPosition(float normalized);
    void syncFocusedStepOptValue(core::state::sequencer::StepProperty property);
    void syncPatternQuickControlOptValue();
    void syncDrumSequencerValues();
    void syncPositions();

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_ui_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& track_bank_;
    oc::api::EncoderAPI& encoders_;
    oc::state::StaticWatchGroup<29> watcher_;

    uint8_t macro_steps_configured_ = 0;
    uint16_t macro_ticks_per_step_configured_ = 0;
    float macro_turns_configured_ = 0.0f;
    std::array<float, core::state::MACRO_COUNT> macro_position_cache_{};
    std::array<bool, core::state::MACRO_COUNT> macro_position_valid_{};

    uint8_t opt_steps_configured_ = 0;
    uint16_t opt_ticks_per_step_configured_ = 0;
    float opt_turns_configured_ = 0.0f;
    float opt_position_cache_ = 0.0f;
    bool opt_position_valid_ = false;
};

}  // namespace core::context::standalone
