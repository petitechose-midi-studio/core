#include "SequencerPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::sequencer {

void SequencerPlaybackService::handleActiveTrackSwitch_() {
    const uint8_t activeTrack = runtime_active_track_;
    if (activeTrack == last_active_track_) {
        return;
    }

    last_playhead_ = activeRuntimeState_().playheadStep;
    last_active_track_ = activeTrack;
}

FLASHMEM SequencerPlaybackService::SequencerPlaybackService(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank,
    core::state::StatusBarState& statusBar,
    oc::api::MidiAPI& midi
)
    : sequencer_(sequencer)
    , status_bar_(statusBar)
{
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        track_outputs_[i] = std::make_unique<SequencerMidiOutput>(midi, i, &note_activity_observer_);
        track_engines_[i] =
            std::make_unique<oc::note::sequencer::StepSequencerEngine>(
                track_runtime_states_[i],
                *track_outputs_[i]
            );
    }
    last_active_track_ = trackBank.activeTrack.get();
    runtime_active_track_ = last_active_track_;
    runtime_enabled_mask_ = trackBank.enabledMask.get();
    core::state::sequencer::SequencerTrackBankSnapshot snapshot;
    core::state::sequencer::captureTrackBankSnapshot(trackBank, sequencer, snapshot);
    syncRuntimeStates_(snapshot);
    publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
}

void SequencerPlaybackService::update(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
                                      uint32_t tick,
                                      bool playing,
                                      uint32_t nowMs,
                                      bool publishRuntimeState,
                                      bool emitLogs) {
    const uint32_t startUs = core::time_compat::micros();
    pending_note_activity_.reset();
    pending_ui_projection_.reset();
    syncRuntimeStates_(snapshot);

    handleActiveTrackSwitch_();
    if (!playing) {
        for (auto& trackEngine : track_engines_) {
            if (trackEngine) {
                trackEngine->update(tick, false);
            }
        }
        last_playhead_ = -1;
        if (publishRuntimeState) {
            publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
        }
        const uint32_t updateUs = core::time_compat::micros() - startUs;
        recordProfilingWindow(tick, false, updateUs, nowMs);
        if (emitLogs) {
            maybeLogProfilingWindow(nowMs);
        }
        return;
    }

    for (uint8_t i = 0; i < track_engines_.size(); ++i) {
        auto& trackEngine = track_engines_[i];
        if (!trackEngine) continue;
        trackEngine->update(
            tick,
            (runtime_enabled_mask_ & static_cast<uint8_t>(1U << i)) != 0
        );
    }

    collectUiProjection_();

    if (publishRuntimeState) {
        publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
    }

    const auto& activeRuntime = activeRuntimeState_();
    const int16_t playhead = activeRuntime.playheadStep;
    if (playhead >= 0 && playhead != last_playhead_) {
        const uint8_t spb = activeRuntime.stepsPerBeat;
        if (spb > 0 && (static_cast<uint8_t>(playhead) % spb) == 0) {
            pending_ui_projection_.beatPulse = true;
        }
    }
    last_playhead_ = playhead;
    const uint32_t updateUs = core::time_compat::micros() - startUs;
    recordProfilingWindow(tick, true, updateUs, nowMs);
    if (emitLogs) {
        maybeLogProfilingWindow(nowMs);
    }
}

FLASHMEM void SequencerPlaybackService::stop() {
    for (auto& trackEngine : track_engines_) {
        if (trackEngine) {
            trackEngine->reset();
        }
    }
    publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
    last_playhead_ = -1;
    pending_ui_projection_.reset();
}

void SequencerPlaybackService::syncRuntimeStates_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot
) {
    runtime_active_track_ =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(snapshot.activeTrack);
    runtime_enabled_mask_ = snapshot.enabledMask;

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        const auto trackSignature = captureRuntimeStateSignature(snapshot.tracks[i]);
        if (track_runtime_signatures_[i].matches(trackSignature)) {
            continue;
        }

        syncRuntimeState(track_runtime_states_[i], snapshot.tracks[i]);
        track_runtime_signatures_[i] = trackSignature;
    }
}

void SequencerPlaybackService::collectUiProjection_() {
    if (pending_note_activity_.noteOutActive) {
        pending_ui_projection_.noteOutPulse = true;
    }

    for (uint8_t track = 0; track < pending_note_activity_.trackVelocity.size(); ++track) {
        const uint8_t velocity = pending_note_activity_.trackVelocity[track];
        if (velocity == 0) continue;
        pending_ui_projection_.trackVelocity[track] =
            std::max(pending_ui_projection_.trackVelocity[track], velocity);
    }
}

FLASHMEM void SequencerPlaybackService::publishUiState(uint32_t nowMs) {
    publishUiProjection(takeUiProjectionSnapshot(), nowMs);
}

FLASHMEM void SequencerPlaybackService::publishUiProjection(const UiProjectionSnapshot& projection, uint32_t nowMs) {
    if (projection.noteOutPulse) {
        status_bar_.pulseNoteOut(nowMs);
    }

    if (projection.beatPulse) {
        status_bar_.pulseBeat(nowMs);
    }

    for (uint8_t track = 0; track < projection.trackVelocity.size(); ++track) {
        const uint8_t velocity = projection.trackVelocity[track];
        if (velocity == 0) continue;
        status_bar_.pulseTrackNote(track, velocity, nowMs);
    }
}

FLASHMEM SequencerPlaybackService::UiProjectionSnapshot SequencerPlaybackService::takeUiProjectionSnapshot() {
    UiProjectionSnapshot snapshot{
        .noteOutPulse = pending_ui_projection_.noteOutPulse,
        .beatPulse = pending_ui_projection_.beatPulse,
        .trackVelocity = pending_ui_projection_.trackVelocity,
    };
    pending_ui_projection_.reset();
    return snapshot;
}

const oc::note::sequencer::StepSequencerRuntimeState&
SequencerPlaybackService::activeRuntimeState_() const {
    return track_runtime_states_[runtime_active_track_];
}

oc::note::sequencer::StepSequencerRuntimeState SequencerPlaybackService::copyActiveRuntimeState() const {
    return activeRuntimeState_();
}

FLASHMEM bool SequencerPlaybackService::takeProfilingSnapshot(uint32_t nowMs, ProfilingSnapshot& snapshot) {
    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
        return false;
    }

    if ((nowMs - profiling_.window_start_ms) < 1000) {
        return false;
    }

    snapshot.updateCount = profiling_.update_count;
    snapshot.avgUpdateUs =
        profiling_.update_count > 0 ? (profiling_.total_update_us / profiling_.update_count) : 0;
    snapshot.maxUpdateUs = profiling_.max_update_us;
    snapshot.noteOnCount = profiling_.note_on_count;
    snapshot.noteOffCount = profiling_.note_off_count;
    snapshot.panicNoteOffCount = profiling_.panic_note_off_count;
    snapshot.lateNoteOnCount = profiling_.late_note_on_count;
    snapshot.avgMidiSendUs =
        profiling_.midi_send_count > 0 ? (profiling_.total_midi_send_us / profiling_.midi_send_count) : 0;
    snapshot.maxMidiSendUs = profiling_.max_midi_send_us;
    snapshot.maxTickJump = profiling_.max_tick_jump;
    snapshot.maxNoteBurst = profiling_.max_note_burst;

    profiling_.resetWindow(nowMs);

    return snapshot.noteOnCount > 0 || snapshot.maxUpdateUs >= 1000 || snapshot.maxTickJump > 1;
}

FLASHMEM void SequencerPlaybackService::recordProfilingWindow(uint32_t tick,
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
    profiling_.note_off_count += pending_note_activity_.noteOffCount;
    profiling_.panic_note_off_count += pending_note_activity_.panicNoteOffCount;
    profiling_.max_note_burst = std::max(profiling_.max_note_burst, pending_note_activity_.noteOnCount);
    profiling_.midi_send_count += pending_note_activity_.midiSendCount;
    profiling_.total_midi_send_us += pending_note_activity_.midiSendTotalUs;
    profiling_.max_midi_send_us = std::max(profiling_.max_midi_send_us,
                                           pending_note_activity_.midiSendMaxUs);

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

FLASHMEM void SequencerPlaybackService::maybeLogProfilingWindow(uint32_t nowMs) {
    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
        return;
    }

    if ((nowMs - profiling_.window_start_ms) < 1000) {
        return;
    }

    const uint32_t avgUpdateUs =
        profiling_.update_count > 0 ? (profiling_.total_update_us / profiling_.update_count) : 0;
    const uint32_t avgMidiSendUs =
        profiling_.midi_send_count > 0 ? (profiling_.total_midi_send_us / profiling_.midi_send_count) : 0;

    if (profiling_.note_on_count > 0 || profiling_.max_update_us >= 1000 || profiling_.max_tick_jump > 1) {
        OC_LOG_INFO("[Perf][SequencerPlayback] updates={} avgUpdate={}us maxUpdate={}us noteOns={} noteOffs={} panicOffs={} lateNotes={} midiSendAvg={}us midiSendMax={}us tickJumpMax={} burstMax={}",
                    profiling_.update_count,
                    avgUpdateUs,
                    profiling_.max_update_us,
                    profiling_.note_on_count,
                    profiling_.note_off_count,
                    profiling_.panic_note_off_count,
                    profiling_.late_note_on_count,
                    avgMidiSendUs,
                    profiling_.max_midi_send_us,
                    profiling_.max_tick_jump,
                    profiling_.max_note_burst);
    }

    profiling_.resetWindow(nowMs);
}

}  // namespace core::sequencer
