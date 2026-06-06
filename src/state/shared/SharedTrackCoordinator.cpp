#include "state/shared/SharedTrackCoordinator.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::shared {

namespace {

constexpr uint16_t kSharedTrackMaskAll =
    static_cast<uint16_t>((1U << sequencer::SequencerTrackBankState::TRACK_COUNT) - 1U);

FLASHMEM uint8_t firstEnabledTrack(uint16_t enabledMask) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            return i;
        }
    }
    return 0;
}

}  // namespace

FLASHMEM uint16_t SharedTrackCoordinator::sanitizeEnabledMask(uint16_t enabledMask) {
    const uint16_t sanitized = static_cast<uint16_t>(enabledMask & kSharedTrackMaskAll);
    return sanitized == 0 ? 0x0001 : sanitized;
}

FLASHMEM uint8_t SharedTrackCoordinator::sanitizeActiveTrack(uint16_t enabledMask, uint8_t activeTrack) {
    const uint16_t sanitizedMask = sanitizeEnabledMask(enabledMask);
    const uint8_t clamped = sequencer::SequencerTrackBankState::clampTrackIndex(activeTrack);
    if ((sanitizedMask & static_cast<uint16_t>(1U << clamped)) != 0) {
        return clamped;
    }
    return firstEnabledTrack(sanitizedMask);
}

FLASHMEM SharedTrackCoordinator::Result SharedTrackCoordinator::apply(
    StateRefs state,
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    const uint16_t sanitizedMask = sanitizeEnabledMask(enabledMask);
    const uint8_t sanitizedActive = sanitizeActiveTrack(sanitizedMask, activeTrack);
    const uint16_t previousMask = state.enabledMask.get();
    const uint8_t previousActive = state.activeTrack.get();

    if (previousMask != sanitizedMask) {
        state.enabledMask.set(sanitizedMask);
    }

    state.macroPages.syncSharedTrackState(sanitizedMask, sanitizedActive);

    if (state.sequencerTracks.currentEnabledMask() != sanitizedMask) {
        state.sequencerTracks.enabledMaskSignal().set(sanitizedMask);
    }

    if (state.sequencerTracks.activeTrackIndex() != sanitizedActive) {
        sequencer::switchActiveTrack(state.sequencerTracks, state.sequencer, sanitizedActive);
    }

    if (previousActive != sanitizedActive) {
        state.activeTrack.set(sanitizedActive);
    }

    return Result{
        sanitizedMask,
        sanitizedActive,
        previousMask != state.enabledMask.get() || previousActive != state.activeTrack.get(),
    };
}

FLASHMEM SharedTrackCoordinator::Result SharedTrackCoordinator::refreshFromMacroPages(StateRefs state) {
    return apply(
        state,
        state.macroPages.currentTrackEnabledMask(),
        state.macroPages.currentActiveTrack()
    );
}

FLASHMEM SharedTrackCoordinator::Result SharedTrackCoordinator::refreshFromSequencer(StateRefs state) {
    return apply(
        state,
        state.sequencerTracks.currentEnabledMask(),
        state.sequencerTracks.activeTrackIndex()
    );
}

}  // namespace core::state::shared
