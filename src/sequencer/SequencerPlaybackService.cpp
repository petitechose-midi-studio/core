#include "SequencerPlaybackService.hpp"

namespace core::sequencer {

SequencerPlaybackService::SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                                                   core::state::StatusBarState& statusBar,
                                                   oc::api::MidiAPI& midi)
    : sequencer_(sequencer)
    , status_bar_(statusBar)
    , output_(midi, statusBar)
    , engine_(sequencer, output_) {}

void SequencerPlaybackService::update(uint32_t tick, bool playing) {
    if (!playing) {
        engine_.update(tick, false);
        last_playhead_ = -1;
        return;
    }

    engine_.update(tick, true);

    const int16_t playhead = sequencer_.playheadStep.get();
    if (playing && playhead >= 0 && playhead != last_playhead_) {
        const uint8_t spb = sequencer_.stepsPerBeat.get();
        if (spb > 0 && (static_cast<uint8_t>(playhead) % spb) == 0) {
            status_bar_.pulseBeat();
        }
    }
    last_playhead_ = playhead;
}

void SequencerPlaybackService::stop() {
    engine_.reset();
    last_playhead_ = -1;
}

}  // namespace core::sequencer
