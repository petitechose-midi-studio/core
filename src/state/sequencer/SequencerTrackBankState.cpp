#include "state/sequencer/SequencerTrackBankState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

FLASHMEM uint8_t firstEnabledTrack(uint16_t enabledMask) {
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            return i;
        }
    }
    return 0;
}

}  // namespace

FLASHMEM SequencerTrackBankState::SequencerTrackBankState()
    : active_track_{0}
    , enabled_mask_{0x0001}
    , muted_mask_{0}
    , project_scale_revision_{0}
    , project_scale_settings_{defaultProjectScaleSettings()}
    , tracks_{} {}

FLASHMEM uint16_t SequencerTrackBankState::sanitizeEnabledMask(uint16_t enabledMask) {
    constexpr uint16_t availableMask =
        static_cast<uint16_t>((1U << TRACK_COUNT) - 1U);
    const uint16_t sanitized = static_cast<uint16_t>(enabledMask & availableMask);
    return sanitized == 0 ? 0x0001 : sanitized;
}

FLASHMEM uint16_t SequencerTrackBankState::sanitizeMutedMask(uint16_t mutedMask,
                                                             uint16_t enabledMask) {
    return static_cast<uint16_t>(mutedMask & sanitizeEnabledMask(enabledMask));
}

FLASHMEM uint8_t SequencerTrackBankState::sanitizeActiveTrack(uint16_t enabledMask,
                                                              uint8_t activeTrack) {
    const uint16_t sanitizedMask = sanitizeEnabledMask(enabledMask);
    const uint8_t clamped = clampTrackIndex(activeTrack);
    return (sanitizedMask & static_cast<uint16_t>(1U << clamped)) != 0
        ? clamped
        : firstEnabledTrack(sanitizedMask);
}

FLASHMEM void SequencerTrackBankState::syncSharedTrackState(uint16_t enabledMaskIn, uint8_t activeTrackIn) {
    const uint16_t sanitizedMask = sanitizeEnabledMask(enabledMaskIn);
    const uint8_t sanitizedActive = sanitizeActiveTrack(sanitizedMask, activeTrackIn);

    if (enabled_mask_.get() != sanitizedMask) {
        enabled_mask_.set(sanitizedMask);
    }
    const uint16_t sanitizedMutedMask = sanitizeMutedMask(muted_mask_.get(), sanitizedMask);
    if (muted_mask_.get() != sanitizedMutedMask) {
        muted_mask_.set(sanitizedMutedMask);
    }
    if (active_track_.get() != sanitizedActive) {
        active_track_.set(sanitizedActive);
    }
}

FLASHMEM bool SequencerTrackBankState::setMutedMask(uint16_t mutedMask) {
    const uint16_t sanitized = sanitizeMutedMask(mutedMask, enabled_mask_.get());
    if (muted_mask_.get() == sanitized) {
        return false;
    }

    muted_mask_.set(sanitized);
    return true;
}

FLASHMEM bool SequencerTrackBankState::setTrackMuted(uint8_t index, bool muted) {
    const uint8_t clamped = clampTrackIndex(index);
    const uint16_t bit = static_cast<uint16_t>(1U << clamped);
    if ((enabled_mask_.get() & bit) == 0) {
        return false;
    }

    const uint16_t current = muted_mask_.get();
    const uint16_t next = muted
        ? static_cast<uint16_t>(current | bit)
        : static_cast<uint16_t>(current & static_cast<uint16_t>(~bit));
    return setMutedMask(next);
}

FLASHMEM bool SequencerTrackBankState::setProjectScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    settings.clamp();
    auto current = project_scale_settings_;
    current.clamp();
    if (current.root == settings.root &&
        current.type == settings.type &&
        current.mode == settings.mode) {
        return false;
    }

    project_scale_settings_ = settings;
    project_scale_revision_.set(project_scale_revision_.get() + 1U);
    for (auto& track : tracks_) {
        if (!isPatternScaleOverride(track.scalePolicy)) {
            track.bumpPatternScaleRevision();
        }
    }
    return true;
}

FLASHMEM void SequencerTrackBankState::reset() {
    syncSharedTrackState(0x0001, 0);
    muted_mask_.set(0);
    project_scale_settings_ = defaultProjectScaleSettings();
    project_scale_revision_.set(0);

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        auto& seq = tracks_[i];
        seq.reset();
        seq.midiChannel.set(i);
    }
}

}  // namespace core::state::sequencer
