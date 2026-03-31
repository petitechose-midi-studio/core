#pragma once

#include <array>
#include <memory>
#include <algorithm>

#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/note/sequencer/ISequencerOutput.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>

#include "config/TimeCompat.hpp"
#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::sequencer {

/**
 * @brief v0 sequencer playback service (global, view-independent)
 *
 * Runs whenever `statusBar.playing` is true.
 */
class SequencerPlaybackService {
public:
    static constexpr uint8_t TRACK_COUNT = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                             core::state::sequencer::SequencerTrackBankState& trackBank,
                             core::state::StatusBarState& statusBar,
                             oc::api::MidiAPI& midi);

    void update(uint32_t tick, bool playing, uint32_t nowMs);
    void stop();

private:
    struct ProfilingWindow {
        uint32_t window_start_ms = 0;
        uint32_t update_count = 0;
        uint32_t total_update_us = 0;
        uint32_t max_update_us = 0;
        uint32_t note_on_count = 0;
        uint32_t late_note_on_count = 0;
        uint32_t max_tick_jump = 0;
        uint32_t max_note_burst = 0;
        uint32_t note_send_count = 0;
        uint32_t total_note_send_us = 0;
        uint32_t max_note_send_us = 0;

        void resetWindow(uint32_t nowMs) {
            window_start_ms = nowMs;
            update_count = 0;
            total_update_us = 0;
            max_update_us = 0;
            note_on_count = 0;
            late_note_on_count = 0;
            max_tick_jump = 0;
            max_note_burst = 0;
            note_send_count = 0;
            total_note_send_us = 0;
            max_note_send_us = 0;
        }
    };

    struct PendingNoteActivity {
        bool noteOutActive = false;
        uint32_t noteOnCount = 0;
        uint32_t noteSendTotalUs = 0;
        uint32_t noteSendMaxUs = 0;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};

        void reset() {
            noteOutActive = false;
            noteOnCount = 0;
            noteSendTotalUs = 0;
            noteSendMaxUs = 0;
            trackVelocity.fill(0);
        }

        void record(uint8_t trackIndex, uint8_t velocity, uint32_t sendUs) {
            noteOutActive = true;
            noteOnCount += 1;
            noteSendTotalUs += sendUs;
            noteSendMaxUs = std::max(noteSendMaxUs, sendUs);
            if (trackIndex >= TRACK_COUNT) return;
            trackVelocity[trackIndex] = std::max(trackVelocity[trackIndex], velocity);
        }
    };

    class MidiOutput final : public oc::note::sequencer::ISequencerOutput {
    public:
        MidiOutput(oc::api::MidiAPI& midi, PendingNoteActivity& pendingNoteActivity, uint8_t trackIndex)
            : midi_(midi)
            , pending_note_activity_(pendingNoteActivity)
            , track_index_(trackIndex) {}

        void setTrackIndex(uint8_t trackIndex) { track_index_ = trackIndex; }

        void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
            const uint32_t startUs = core::time_compat::micros();
            midi_.sendNoteOn(channel, note, velocity);
            pending_note_activity_.record(track_index_,
                                          velocity,
                                          core::time_compat::micros() - startUs);
        }

        void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
            midi_.sendNoteOff(channel, note, velocity);
        }

        void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override {
            midi_.sendCC(channel, cc, value);
        }

        void allNotesOff() override {
            midi_.allNotesOff();
        }

    private:
        oc::api::MidiAPI& midi_;
        PendingNoteActivity& pending_note_activity_;
        uint8_t track_index_ = 0;
    };

    void recordProfilingWindow(uint32_t tick, bool playing, uint32_t updateUs, uint32_t nowMs);
    void maybeLogProfilingWindow(uint32_t nowMs);
    void drainPendingNoteActivity(uint32_t nowMs);

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& track_bank_;
    core::state::StatusBarState& status_bar_;
    PendingNoteActivity pending_note_activity_{};
    ProfilingWindow profiling_{};
    MidiOutput active_output_;
    oc::note::sequencer::StepSequencerEngine engine_;
    std::array<std::unique_ptr<MidiOutput>, TRACK_COUNT> track_outputs_{};
    std::array<std::unique_ptr<oc::note::sequencer::StepSequencerEngine>, TRACK_COUNT> track_engines_{};

    int16_t last_playhead_ = -1;
    uint8_t last_active_track_ = 0;
    uint32_t last_tick_ = 0;
    bool last_tick_valid_ = false;
};

}  // namespace core::sequencer
