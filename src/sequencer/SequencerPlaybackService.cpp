#include "SequencerPlaybackService.hpp"

namespace core::sequencer {

SequencerPlaybackService::SequencerPlaybackService(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank,
    core::state::StatusBarState& statusBar,
    oc::api::MidiAPI& midi
)
    : sequencer_(sequencer)
    , track_bank_(trackBank)
    , status_bar_(statusBar)
    , active_output_(midi, statusBar, trackBank.activeTrack.get())
    , engine_(sequencer, active_output_) {
    for (uint8_t i = 0; i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        track_outputs_[i] = std::make_unique<MidiOutput>(midi, statusBar, i);
        track_engines_[i] =
            std::make_unique<oc::note::sequencer::StepSequencerEngine>(
                track_bank_.track(i),
                *track_outputs_[i]
            );
    }
    last_active_track_ = track_bank_.activeTrack.get();
}

void SequencerPlaybackService::update(uint32_t tick, bool playing, uint32_t nowMs) {
    active_output_.setNowMs(nowMs);
    for (auto& trackOutput : track_outputs_) {
        if (trackOutput) {
            trackOutput->setNowMs(nowMs);
        }
    }

    const uint8_t activeTrack = track_bank_.activeTrack.get();
    if (activeTrack != last_active_track_) {
        stop();
        active_output_.setTrackIndex(activeTrack);
        last_active_track_ = activeTrack;
    }

    if (!playing) {
        engine_.update(tick, false);
        for (auto& trackEngine : track_engines_) {
            if (trackEngine) {
                trackEngine->update(tick, false);
            }
        }
        last_playhead_ = -1;
        return;
    }

    engine_.update(tick, track_bank_.isTrackEnabled(activeTrack));
    for (uint8_t i = 0; i < track_engines_.size(); ++i) {
        auto& trackEngine = track_engines_[i];
        if (!trackEngine) continue;
        trackEngine->update(
            tick,
            (i != activeTrack) && track_bank_.isTrackEnabled(i)
        );
    }

    const int16_t playhead = sequencer_.playheadStep.get();
    if (playhead >= 0 && playhead != last_playhead_) {
        const uint8_t spb = sequencer_.stepsPerBeat.get();
        if (spb > 0 && (static_cast<uint8_t>(playhead) % spb) == 0) {
            status_bar_.pulseBeat(nowMs);
        }
    }
    last_playhead_ = playhead;
}

void SequencerPlaybackService::stop() {
    engine_.reset();
    for (auto& trackEngine : track_engines_) {
        if (trackEngine) {
            trackEngine->reset();
        }
    }
    last_playhead_ = -1;
}

}  // namespace core::sequencer
