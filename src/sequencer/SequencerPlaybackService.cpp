#include "SequencerPlaybackService.hpp"

namespace core::sequencer {

SequencerPlaybackService::SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                                                   core::state::StatusBarState& statusBar,
                                                   oc::api::MidiAPI& midi)
    : sequencer_(sequencer)
    , status_bar_(statusBar)
    , output_(midi, statusBar)
    , engine_(static_cast<oc::note::sequencer::StepSequencerState&>(sequencer), output_) {}

void SequencerPlaybackService::update(uint32_t tick, bool playing) {
    last_tick_ = tick;
    engine_.update(tick, playing);

    const int16_t playhead = sequencer_.playheadStep.get();
    if (playing && playhead >= 0 && playhead != last_playhead_) {
        const uint8_t spb = sequencer_.stepsPerBeat.get();
        if (spb > 0 && (static_cast<uint8_t>(playhead) % spb) == 0) {
            status_bar_.beatPulse.set(true);
        }
    }
    last_playhead_ = playhead;
    if (!playing) last_playhead_ = -1;
}

void SequencerPlaybackService::stop() {
    engine_.update(last_tick_, false);
    engine_.reset();
    last_tick_ = 0;
    last_playhead_ = -1;
}

}  // namespace core::sequencer
