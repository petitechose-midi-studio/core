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

FLASHMEM uint32_t nextRevision(uint32_t current) {
    uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

}  // namespace

FLASHMEM SequencerTrackBankState::SequencerTrackBankState()
    : active_track_{0}
    , enabled_mask_{0x0001}
    , project_scale_revision_{0}
    , project_scale_settings_{defaultProjectScaleSettings()}
    , tracks_{} {
    for (auto& drumTrack : drum_tracks_) drumTrack.reset();
    drum_track_revisions_.fill(1U);
}

FLASHMEM uint16_t SequencerTrackBankState::sanitizeEnabledMask(uint16_t enabledMask) {
    constexpr uint16_t availableMask =
        static_cast<uint16_t>((1U << TRACK_COUNT) - 1U);
    const uint16_t sanitized = static_cast<uint16_t>(enabledMask & availableMask);
    return sanitized == 0 ? 0x0001 : sanitized;
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
    if (active_track_.get() != sanitizedActive) {
        active_track_.set(sanitizedActive);
    }
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
    const uint8_t activeTrack = activeTrackIndex();
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        // The active bank slot is noncanonical scratch. The editor owns the
        // active Pattern and its revision while that Track is selected.
        if (i == activeTrack) continue;
        auto& track = tracks_[i];
        if (!isPatternScaleOverride(track.scalePolicy)) {
            track.bumpPatternScaleRevision();
        }
    }
    return true;
}

FLASHMEM void SequencerTrackBankState::publishDrumMutation(uint8_t index) {
    const uint8_t trackIndex = clampTrackIndex(index);
    drum_track_revisions_[trackIndex] = nextRevision(
        drum_track_revisions_[trackIndex]);
    drum_revision_.set(nextRevision(drum_revision_.get()));
}

FLASHMEM bool SequencerTrackBankState::setTrackKind(
    uint8_t index,
    SequencerTrackKind kind,
    bool resetPayload,
    DrumKitPreset drumPreset
) {
    const uint8_t trackIndex = clampTrackIndex(index);
    const uint16_t bit = static_cast<uint16_t>(1U << trackIndex);
    const uint16_t nextMask = kind == SequencerTrackKind::DRUM
        ? static_cast<uint16_t>(drum_track_mask_ | bit)
        : static_cast<uint16_t>(drum_track_mask_ & static_cast<uint16_t>(~bit));
    const bool kindChanged = nextMask != drum_track_mask_;
    if (resetPayload) {
        drum_tracks_[trackIndex].reset(drumPreset);
    }
    if (!kindChanged && !resetPayload) return false;
    drum_track_mask_ = nextMask;
    publishDrumMutation(trackIndex);
    return true;
}

FLASHMEM void SequencerTrackBankState::restoreDrumTrack(
    uint8_t index,
    SequencerTrackKind kind,
    const DrumTrackState& state
) {
    const uint8_t trackIndex = clampTrackIndex(index);
    const uint16_t bit = static_cast<uint16_t>(1U << trackIndex);
    drum_track_mask_ = kind == SequencerTrackKind::DRUM
        ? static_cast<uint16_t>(drum_track_mask_ | bit)
        : static_cast<uint16_t>(
              drum_track_mask_ & static_cast<uint16_t>(~bit));
    drum_tracks_[trackIndex] = state;
    publishDrumMutation(trackIndex);
}

FLASHMEM void SequencerTrackBankState::captureDrumTrackBank(
    DrumTrackBankSnapshot& out
) const {
    out.drumTrackMask = drum_track_mask_;
    out.tracks = drum_tracks_;
}

FLASHMEM bool SequencerTrackBankState::applyDrumTrackBank(
    const DrumTrackBankSnapshot& snapshot
) {
    drum_track_mask_ = snapshot.drumTrackMask;
    drum_tracks_ = snapshot.tracks;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        drum_track_revisions_[track] = nextRevision(
            drum_track_revisions_[track]);
    }
    drum_revision_.set(nextRevision(drum_revision_.get()));
    return true;
}

FLASHMEM void SequencerTrackBankState::clearDrumTrackBank() {
    drum_track_mask_ = 0U;
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        drum_tracks_[track].reset();
        drum_track_revisions_[track] = nextRevision(
            drum_track_revisions_[track]);
    }
    drum_revision_.set(nextRevision(drum_revision_.get()));
}

FLASHMEM void SequencerTrackBankState::reset() {
    syncSharedTrackState(0x0001, 0);
    project_scale_settings_ = defaultProjectScaleSettings();
    project_scale_revision_.set(0);

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        auto& seq = tracks_[i];
        seq.reset();
    }
    clearDrumTrackBank();
}

}  // namespace core::state::sequencer
