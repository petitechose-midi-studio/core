#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

struct SequencerTrackSelectorState {
    Signal<bool, 4> selecting{false};
    Signal<uint8_t, 6> selectedTrack{0};
    uint8_t snapshotTrack = 0;
    uint8_t snapshotEnabledMask = 0x01;

    void reset(uint8_t track = 0) {
        selecting.set(false);
        selectedTrack.set(track);
        snapshotTrack = track;
        snapshotEnabledMask = 0x01;
    }
};

struct SequencerTrackBankState {
    static constexpr uint8_t TRACK_COUNT = 8;

    Signal<uint8_t, 8> activeTrack{0};
    Signal<uint8_t, 8> enabledMask{0x01};
    SequencerTrackSelectorState selector;
    std::array<SequencerState, TRACK_COUNT> tracks{};

    static constexpr uint8_t clampTrackIndex(uint8_t track) {
        return (track >= TRACK_COUNT) ? static_cast<uint8_t>(TRACK_COUNT - 1) : track;
    }

    SequencerState& track(uint8_t index) {
        return tracks[clampTrackIndex(index)];
    }

    const SequencerState& track(uint8_t index) const {
        return tracks[clampTrackIndex(index)];
    }

    bool isTrackEnabled(uint8_t index) const {
        const uint8_t clamped = clampTrackIndex(index);
        return (enabledMask.get() & static_cast<uint8_t>(1U << clamped)) != 0;
    }

    void setTrackEnabled(uint8_t index, bool enabled) {
        const uint8_t clamped = clampTrackIndex(index);
        uint8_t mask = enabledMask.get();
        const uint8_t bit = static_cast<uint8_t>(1U << clamped);
        if (enabled) mask |= bit;
        else mask &= static_cast<uint8_t>(~bit);
        enabledMask.set(mask);
    }

    void toggleTrackEnabled(uint8_t index) {
        setTrackEnabled(index, !isTrackEnabled(index));
    }

    void reset() {
        activeTrack.set(0);
        enabledMask.set(0x01);
        selector.reset(0);
        selector.snapshotEnabledMask = 0x01;

        for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
            auto& seq = tracks[i];
            seq.reset();
            seq.midiChannel.set(i);
        }
    }
};

}  // namespace core::state::sequencer
