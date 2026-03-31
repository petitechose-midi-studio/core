#include "SequencerPlaybackService.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

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
    , active_output_(midi, pending_note_activity_, trackBank.activeTrack.get())
    , engine_(sequencer, active_output_) {
    for (uint8_t i = 0; i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        track_outputs_[i] = std::make_unique<MidiOutput>(midi, pending_note_activity_, i);
        track_engines_[i] =
            std::make_unique<oc::note::sequencer::StepSequencerEngine>(
                track_bank_.track(i),
                *track_outputs_[i]
            );
    }
    last_active_track_ = track_bank_.activeTrack.get();
}

void SequencerPlaybackService::update(uint32_t tick, bool playing, uint32_t nowMs) {
    const uint32_t startUs = core::time_compat::micros();
    pending_note_activity_.reset();

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
        recordProfilingWindow(tick, false, core::time_compat::micros() - startUs, nowMs);
        maybeLogProfilingWindow(nowMs);
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

    drainPendingNoteActivity(nowMs);

    const int16_t playhead = sequencer_.playheadStep.get();
    if (playhead >= 0 && playhead != last_playhead_) {
        const uint8_t spb = sequencer_.stepsPerBeat.get();
        if (spb > 0 && (static_cast<uint8_t>(playhead) % spb) == 0) {
            status_bar_.pulseBeat(nowMs);
        }
    }
    last_playhead_ = playhead;
    recordProfilingWindow(tick, true, core::time_compat::micros() - startUs, nowMs);
    maybeLogProfilingWindow(nowMs);
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

void SequencerPlaybackService::drainPendingNoteActivity(uint32_t nowMs) {
    if (pending_note_activity_.noteOutActive) {
        status_bar_.pulseNoteOut(nowMs);
    }

    for (uint8_t track = 0; track < pending_note_activity_.trackVelocity.size(); ++track) {
        const uint8_t velocity = pending_note_activity_.trackVelocity[track];
        if (velocity == 0) continue;
        status_bar_.pulseTrackNote(track, velocity, nowMs);
    }
}

void SequencerPlaybackService::recordProfilingWindow(uint32_t tick,
                                                     bool playing,
                                                     uint32_t updateUs,
                                                     uint32_t nowMs) {
    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
    }

    profiling_.update_count += 1;
    profiling_.total_update_us += updateUs;
    profiling_.max_update_us = std::max(profiling_.max_update_us, updateUs);
    profiling_.note_on_count += pending_note_activity_.noteOnCount;
    profiling_.max_note_burst = std::max(profiling_.max_note_burst, pending_note_activity_.noteOnCount);
    profiling_.note_send_count += pending_note_activity_.noteOnCount;
    profiling_.total_note_send_us += pending_note_activity_.noteSendTotalUs;
    profiling_.max_note_send_us = std::max(profiling_.max_note_send_us,
                                           pending_note_activity_.noteSendMaxUs);

    if (playing && last_tick_valid_) {
        const uint32_t tickJump = (tick >= last_tick_) ? (tick - last_tick_) : 0;
        profiling_.max_tick_jump = std::max(profiling_.max_tick_jump, tickJump);
        if (tickJump > 1) {
            profiling_.late_note_on_count += pending_note_activity_.noteOnCount;
        }
    }

    last_tick_ = tick;
    last_tick_valid_ = true;
}

void SequencerPlaybackService::maybeLogProfilingWindow(uint32_t nowMs) {
    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
        return;
    }

    if ((nowMs - profiling_.window_start_ms) < 1000) {
        return;
    }

    const uint32_t avgUpdateUs =
        profiling_.update_count > 0 ? (profiling_.total_update_us / profiling_.update_count) : 0;
    const uint32_t avgNoteSendUs =
        profiling_.note_send_count > 0 ? (profiling_.total_note_send_us / profiling_.note_send_count) : 0;

    if (profiling_.note_on_count > 0 || profiling_.max_update_us >= 1000 || profiling_.max_tick_jump > 1) {
        OC_LOG_INFO("[Perf][SequencerPlayback] updates={} avgUpdate={}us maxUpdate={}us notes={} lateNotes={} noteSendAvg={}us noteSendMax={}us tickJumpMax={} burstMax={}",
                    profiling_.update_count,
                    avgUpdateUs,
                    profiling_.max_update_us,
                    profiling_.note_on_count,
                    profiling_.late_note_on_count,
                    avgNoteSendUs,
                    profiling_.max_note_send_us,
                    profiling_.max_tick_jump,
                    profiling_.max_note_burst);
    }

    profiling_.resetWindow(nowMs);
}

}  // namespace core::sequencer
