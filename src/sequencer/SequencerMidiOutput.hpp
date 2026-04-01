#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/note/sequencer/ISequencerOutput.hpp>

namespace core::sequencer {

struct SequencerMidiOutputObserver {
    virtual ~SequencerMidiOutputObserver() = default;

    virtual void onNoteOn(uint8_t trackIndex, uint8_t velocity, uint32_t sendUs) = 0;
    virtual void onNoteOff(uint32_t sendUs) = 0;
    virtual void onPanicNoteOffs(uint32_t count, uint32_t totalUs, uint32_t maxUs) = 0;
};

class SequencerMidiOutput final : public oc::note::sequencer::ISequencerOutput {
public:
    static constexpr size_t MAX_ACTIVE_NOTES = 32;

    explicit SequencerMidiOutput(oc::api::MidiAPI& midi,
                                 uint8_t trackIndex,
                                 SequencerMidiOutputObserver* observer = nullptr);

    void setTrackIndex(uint8_t trackIndex) { track_index_ = trackIndex; }

    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override;
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override;
    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override;
    void allNotesOff() override;

private:
    struct ActiveNote {
        uint8_t channel = 0;
        uint8_t note = 0;
        bool active = false;
    };

    void markNoteActive_(uint8_t channel, uint8_t note);
    void markNoteInactive_(uint8_t channel, uint8_t note);

    oc::api::MidiAPI& midi_;
    SequencerMidiOutputObserver* observer_ = nullptr;
    uint8_t track_index_ = 0;
    std::array<ActiveNote, MAX_ACTIVE_NOTES> active_notes_{};
};

}  // namespace core::sequencer
