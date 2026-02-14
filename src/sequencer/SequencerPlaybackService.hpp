#pragma once

#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/note/clock/InternalClock.hpp>
#include <oc/note/sequencer/ISequencerOutput.hpp>
#include <oc/note/sequencer/StepSequencerEngine.hpp>

#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::sequencer {

/**
 * @brief v0 sequencer playback service (global, view-independent)
 *
 * Runs whenever `statusBar.playing` is true.
 */
class SequencerPlaybackService {
public:
    SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                             core::state::StatusBarState& statusBar,
                             oc::api::MidiAPI& midi);

    void update(uint32_t nowMs);
    void stop();

private:
    class MidiOutput final : public oc::note::sequencer::ISequencerOutput {
    public:
        MidiOutput(oc::api::MidiAPI& midi, core::state::StatusBarState& statusBar)
            : midi_(midi)
            , status_bar_(statusBar) {}

        void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
            midi_.sendNoteOn(channel, note, velocity);
            status_bar_.noteOutActive.set(true);
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
    };

    core::state::sequencer::SequencerState& sequencer_;
    core::state::StatusBarState& status_bar_;
    MidiOutput output_;
    oc::note::clock::InternalClock clock_;
    oc::note::sequencer::StepSequencerEngine engine_;

    int16_t last_playhead_ = -1;
};

}  // namespace core::sequencer
