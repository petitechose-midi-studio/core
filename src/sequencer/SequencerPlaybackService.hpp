#pragma once

#include <array>
#include <memory>
#include <algorithm>

#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/note/sequencer/ISequencerOutput.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>

#include "config/TimeCompat.hpp"
#include "sequencer/SequencerMidiOutput.hpp"
#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
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

    struct UiProjectionSnapshot {
        bool noteOutPulse = false;
        bool beatPulse = false;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};
    };

    struct ProfilingSnapshot {
        uint32_t updateCount = 0;
        uint32_t avgUpdateUs = 0;
        uint32_t maxUpdateUs = 0;
        uint32_t noteOnCount = 0;
        uint32_t noteOffCount = 0;
        uint32_t panicNoteOffCount = 0;
        uint32_t lateNoteOnCount = 0;
        uint32_t avgMidiSendUs = 0;
        uint32_t maxMidiSendUs = 0;
        uint32_t maxTickJump = 0;
        uint32_t maxNoteBurst = 0;
    };

    SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                             core::state::sequencer::SequencerTrackBankState& trackBank,
                             core::state::StatusBarState& statusBar,
                             oc::api::MidiAPI& midi);

    void update(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
                uint32_t tick,
                bool playing,
                uint32_t nowMs,
                bool publishRuntimeState = true,
                bool emitLogs = true);
    void stop();
    void publishUiProjection(const UiProjectionSnapshot& projection, uint32_t nowMs);
    UiProjectionSnapshot takeUiProjectionSnapshot();
    oc::note::sequencer::StepSequencerRuntimeState copyActiveRuntimeState() const;
    bool takeProfilingSnapshot(uint32_t nowMs, ProfilingSnapshot& snapshot);

private:
    const oc::note::sequencer::StepSequencerRuntimeState& activeRuntimeState_() const;

    struct ProfilingWindow {
        uint32_t window_start_ms = 0;
        uint32_t update_count = 0;
        uint32_t total_update_us = 0;
        uint32_t max_update_us = 0;
        uint32_t note_on_count = 0;
        uint32_t note_off_count = 0;
        uint32_t panic_note_off_count = 0;
        uint32_t late_note_on_count = 0;
        uint32_t max_tick_jump = 0;
        uint32_t max_note_burst = 0;
        uint32_t midi_send_count = 0;
        uint32_t total_midi_send_us = 0;
        uint32_t max_midi_send_us = 0;

        void resetWindow(uint32_t nowMs) {
            window_start_ms = nowMs;
            update_count = 0;
            total_update_us = 0;
            max_update_us = 0;
            note_on_count = 0;
            note_off_count = 0;
            panic_note_off_count = 0;
            late_note_on_count = 0;
            max_tick_jump = 0;
            max_note_burst = 0;
            midi_send_count = 0;
            total_midi_send_us = 0;
            max_midi_send_us = 0;
        }
    };

    struct PendingNoteActivity {
        bool noteOutActive = false;
        uint32_t noteOnCount = 0;
        uint32_t noteOffCount = 0;
        uint32_t panicNoteOffCount = 0;
        uint32_t midiSendCount = 0;
        uint32_t midiSendTotalUs = 0;
        uint32_t midiSendMaxUs = 0;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};

        void reset() {
            noteOutActive = false;
            noteOnCount = 0;
            noteOffCount = 0;
            panicNoteOffCount = 0;
            midiSendCount = 0;
            midiSendTotalUs = 0;
            midiSendMaxUs = 0;
            trackVelocity.fill(0);
        }

        void recordNoteOn(uint8_t trackIndex, uint8_t velocity, uint32_t sendUs) {
            noteOutActive = true;
            noteOnCount += 1;
            midiSendCount += 1;
            midiSendTotalUs += sendUs;
            midiSendMaxUs = std::max(midiSendMaxUs, sendUs);
            if (trackIndex >= TRACK_COUNT) return;
            trackVelocity[trackIndex] = std::max(trackVelocity[trackIndex], velocity);
        }

        void recordNoteOff(uint32_t sendUs) {
            noteOffCount += 1;
            midiSendCount += 1;
            midiSendTotalUs += sendUs;
            midiSendMaxUs = std::max(midiSendMaxUs, sendUs);
        }

        void recordPanicNoteOffs(uint32_t count, uint32_t totalUs, uint32_t maxUs) {
            panicNoteOffCount += count;
            midiSendCount += count;
            midiSendTotalUs += totalUs;
            midiSendMaxUs = std::max(midiSendMaxUs, maxUs);
        }
    };

    class PendingNoteActivityObserver final : public SequencerMidiOutputObserver {
    public:
        explicit PendingNoteActivityObserver(PendingNoteActivity& pendingNoteActivity)
            : pending_note_activity_(pendingNoteActivity) {}

        void onNoteOn(uint8_t trackIndex, uint8_t velocity, uint32_t sendUs) override {
            pending_note_activity_.recordNoteOn(trackIndex, velocity, sendUs);
        }

        void onNoteOff(uint32_t sendUs) override {
            pending_note_activity_.recordNoteOff(sendUs);
        }

        void onPanicNoteOffs(uint32_t count, uint32_t totalUs, uint32_t maxUs) override {
            pending_note_activity_.recordPanicNoteOffs(count, totalUs, maxUs);
        }

    private:
        PendingNoteActivity& pending_note_activity_;
    };

    struct PendingUiProjection {
        bool noteOutPulse = false;
        bool beatPulse = false;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};

        void reset() {
            noteOutPulse = false;
            beatPulse = false;
            trackVelocity.fill(0);
        }
    };

    void handleActiveTrackSwitch_();
    void syncRuntimeStates_(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot);
    void recordProfilingWindow(uint32_t tick, bool playing, uint32_t updateUs, uint32_t nowMs);
    void maybeLogProfilingWindow(uint32_t nowMs);
    void collectUiProjection_();

public:
    void publishUiState(uint32_t nowMs);

private:

    core::state::sequencer::SequencerState& sequencer_;
    core::state::StatusBarState& status_bar_;
    PendingNoteActivity pending_note_activity_{};
    PendingUiProjection pending_ui_projection_{};
    PendingNoteActivityObserver note_activity_observer_{pending_note_activity_};
    ProfilingWindow profiling_{};
    std::array<oc::note::sequencer::StepSequencerRuntimeState, TRACK_COUNT> track_runtime_states_{};
    std::array<SequencerRuntimeStateSignature, TRACK_COUNT> track_runtime_signatures_{};
    std::array<std::unique_ptr<SequencerMidiOutput>, TRACK_COUNT> track_outputs_{};
    std::array<std::unique_ptr<oc::note::sequencer::StepSequencerEngine>, TRACK_COUNT> track_engines_{};
    uint8_t runtime_active_track_ = 0;
    uint16_t runtime_enabled_mask_ = 0x0001;

    int16_t last_playhead_ = -1;
    uint8_t last_active_track_ = 0;
    uint32_t last_tick_ = 0;
    bool last_tick_valid_ = false;
};

}  // namespace core::sequencer
