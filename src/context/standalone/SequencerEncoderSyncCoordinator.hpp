#pragma once

#include <array>

#include <oc/state/SignalWatcher.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/CoreState.hpp"

namespace oc::api {
class EncoderAPI;
}

namespace core::context::standalone {

class SequencerEncoderSyncCoordinator {
public:
    SequencerEncoderSyncCoordinator(core::state::CoreState& state, oc::api::EncoderAPI& encoders);

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

    core::state::CoreState& state_;
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
