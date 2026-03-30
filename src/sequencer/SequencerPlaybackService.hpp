#pragma once

#include <array>
#include <memory>

#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/note/sequencer/ISequencerOutput.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>

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
    class MidiOutput final : public oc::note::sequencer::ISequencerOutput {
    public:
        MidiOutput(oc::api::MidiAPI& midi, core::state::StatusBarState& statusBar, uint8_t trackIndex)
            : midi_(midi)
            , status_bar_(statusBar)
            , track_index_(trackIndex) {}

        void setTrackIndex(uint8_t trackIndex) { track_index_ = trackIndex; }
        void setNowMs(uint32_t nowMs) { now_ms_ = nowMs; }

        void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
            midi_.sendNoteOn(channel, note, velocity);
            status_bar_.pulseNoteOut(now_ms_);
            status_bar_.pulseTrackNote(track_index_, velocity, now_ms_);
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
        core::state::StatusBarState& status_bar_;
        uint8_t track_index_ = 0;
        uint32_t now_ms_ = 0;
    };

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& track_bank_;
    core::state::StatusBarState& status_bar_;
    MidiOutput active_output_;
    oc::note::sequencer::StepSequencerEngine engine_;
    std::array<std::unique_ptr<MidiOutput>, TRACK_COUNT> track_outputs_{};
    std::array<std::unique_ptr<oc::note::sequencer::StepSequencerEngine>, TRACK_COUNT> track_engines_{};

    int16_t last_playhead_ = -1;
    uint8_t last_active_track_ = 0;
};

}  // namespace core::sequencer
