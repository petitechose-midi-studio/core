#include "state/sequencer/SequencerTrackBankState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

uint16_t sanitizeEnabledMask(uint16_t enabledMask) {
    const uint16_t availableMask = static_cast<uint16_t>((1U << SequencerTrackBankState::TRACK_COUNT) - 1U);
    const uint16_t sanitized = static_cast<uint16_t>(enabledMask & availableMask);
    return sanitized == 0 ? 0x0001 : sanitized;
}

uint8_t firstEnabledTrack(uint16_t enabledMask) {
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            return i;
        }
    }
    return 0;
}

uint8_t sanitizeActiveTrack(uint16_t enabledMask, uint8_t activeTrack) {
    const uint8_t clamped = SequencerTrackBankState::clampTrackIndex(activeTrack);
    return (enabledMask & static_cast<uint16_t>(1U << clamped)) != 0
        ? clamped
        : firstEnabledTrack(enabledMask);
}

}  // namespace

FLASHMEM SequencerTrackBankState::SequencerTrackBankState()
    : active_track_{0}, enabled_mask_{0x0001}, tracks_{} {}

FLASHMEM void SequencerTrackBankState::syncSharedTrackState(uint16_t enabledMaskIn, uint8_t activeTrackIn) {
    const uint16_t sanitizedMask = sanitizeEnabledMask(enabledMaskIn);
    const uint8_t sanitizedActive = sanitizeActiveTrack(sanitizedMask, activeTrackIn);

    if (enabled_mask_.get() != sanitizedMask) {
        enabled_mask_.set(sanitizedMask);
    }
    if (active_track_.get() != sanitizedActive) {
        active_track_.set(sanitizedActive);
    }
}

FLASHMEM void SequencerTrackBankState::reset() {
    syncSharedTrackState(0x0001, 0);

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        auto& seq = tracks_[i];
        seq.reset();
        seq.midiChannel.set(i);
    }
}

}  // namespace core::state::sequencer
