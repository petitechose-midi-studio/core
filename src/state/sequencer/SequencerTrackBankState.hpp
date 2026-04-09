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
    uint16_t snapshotEnabledMask = 0x0001;

    void reset(uint8_t track = 0);
};

struct SequencerTrackBankState {
    static constexpr uint8_t TRACK_COUNT = 16;

    Signal<uint8_t, 8> activeTrack{0};
    Signal<uint16_t, 16> enabledMask{0x0001};
    SequencerTrackSelectorState selector;
    std::array<SequencerState, TRACK_COUNT> tracks{};

    SequencerTrackBankState();

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
        return (enabledMask.get() & static_cast<uint16_t>(1U << clamped)) != 0;
    }

    void setTrackEnabled(uint8_t index, bool enabled) {
        const uint8_t clamped = clampTrackIndex(index);
        uint16_t mask = enabledMask.get();
        const uint16_t bit = static_cast<uint16_t>(1U << clamped);
        if (enabled) mask |= bit;
        else mask &= static_cast<uint16_t>(~bit);
        enabledMask.set(mask);
    }

    void toggleTrackEnabled(uint8_t index) {
        setTrackEnabled(index, !isTrackEnabled(index));
    }

    void reset();
};

}  // namespace core::state::sequencer
