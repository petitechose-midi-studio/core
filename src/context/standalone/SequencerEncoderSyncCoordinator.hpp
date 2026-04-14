#pragma once

#include <array>

#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>
#include <oc/state/SignalWatcher.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/MacroState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace oc::api {
class EncoderAPI;
}

namespace core::context::standalone {

class SequencerEncoderSyncCoordinator {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::sequencer::SequencerState& sequencer;
    };

    SequencerEncoderSyncCoordinator(StateRefs state, oc::api::EncoderAPI& encoders);

    void bind();
    void reset();
    void syncNow();

private:
    void resetOptCache();
    void ensureMacroEncoderConfig(
        const core::handler::sequencer::input_utils::StepPropertyEncoderConfig& config
    );
    void syncMacroEncoderValues(
        uint8_t page,
        core::state::sequencer::StepProperty property
    );
    void ensureOptEncoderConfig(
        const core::handler::sequencer::input_utils::StepPropertyEncoderConfig& config
    );
    void syncOptEncoderValue(
        uint8_t length,
        uint8_t focusedStep,
        core::state::sequencer::StepProperty property
    );
    void syncPositions();

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    oc::state::Signal<core::ui::ViewType, 8>& active_view_;
    core::state::sequencer::SequencerState& sequencer_;
    oc::api::EncoderAPI& encoders_;
    oc::state::SignalWatcher watcher_;

    uint8_t macro_steps_configured_ = 0;
    uint8_t opt_steps_configured_ = 0;
    uint16_t macro_ticks_per_step_configured_ = 0;
    uint16_t opt_ticks_per_step_configured_ = 0;
    float macro_turns_configured_ = 0.0f;
    float opt_turns_configured_ = 0.0f;
    std::array<float, core::state::MACRO_COUNT> macro_position_cache_{};
    std::array<bool, core::state::MACRO_COUNT> macro_position_valid_{};
    float opt_position_cache_ = 0.0f;
    bool opt_position_valid_ = false;
};

}  // namespace core::context::standalone
