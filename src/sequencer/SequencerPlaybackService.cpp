#include "SequencerPlaybackService.hpp"

#include <oc/time/Time.hpp>

namespace core::sequencer {

SequencerPlaybackService::SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                                                   core::state::StatusBarState& statusBar,
                                                   oc::api::MidiAPI& midi)
    : sequencer_(sequencer)
    , status_bar_(statusBar)
    , output_(midi, statusBar)
    , engine_(static_cast<oc::note::sequencer::StepSequencerState&>(sequencer), output_) {}

void SequencerPlaybackService::update(uint32_t nowMs) {
    const bool playing = status_bar_.playing.get();

    clock_.setBpm(status_bar_.tempo.get());
    clock_.setPlaying(playing);
    clock_.update(nowMs);

    engine_.update(clock_.tick(), playing);

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
    engine_.update(clock_.tick(), false);
    engine_.reset();
    clock_.reset();
    last_playhead_ = -1;
}

}  // namespace core::sequencer
