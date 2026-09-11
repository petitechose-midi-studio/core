#include "state/sequencer/SequencerTrackBankState.hpp"

#include <cstring>

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
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
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

FLASHMEM bool SequencerTrackBankState::matchesDrumTrack(
    uint8_t index, const DrumTrackState* source
) const noexcept {
    const auto* live = drumTrackIfPresent(index);
    return live && source ? std::memcmp(live, source, sizeof(*live)) == 0 : live == source;
}

FLASHMEM bool SequencerTrackBankState::setTrackKind(
    uint8_t index,
    SequencerTrackKind kind,
    bool resetPayload,
    DrumKitPreset drumPreset
) {
    if (!resetPayload && trackKind(index) == kind) return false;
    DrumTrackPtr owner;
    if (kind == SequencerTrackKind::DRUM) {
        owner = core::app::makeExtmemUniqueCold<DrumTrackState>();
        if (!owner) return false;
        owner->reset(drumPreset);
    }
    installDrumTrack(index, std::move(owner));
    return true;
}

FLASHMEM void SequencerTrackBankState::exchangeDrumTrack(
    uint8_t index, DrumTrackPtr& owner
) noexcept {
    const uint8_t trackIndex = clampTrackIndex(index);
    const uint16_t bit = static_cast<uint16_t>(1U << trackIndex);
    drum_tracks_[trackIndex].swap(owner);
    drum_track_mask_ = drum_tracks_[trackIndex]
        ? static_cast<uint16_t>(drum_track_mask_ | bit)
        : static_cast<uint16_t>(drum_track_mask_ & ~bit);
    publishDrumMutation(trackIndex);
}

FLASHMEM void SequencerTrackBankState::captureDrumTrackBank(
    DrumTrackBankSnapshot& out
) const {
    out.drumTrackMask = drum_track_mask_;
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        if (drum_tracks_[i]) out.tracks[i] = *drum_tracks_[i];
        else out.tracks[i].reset();
    }
}

FLASHMEM bool prepareDrumTrackBank(
    const DrumTrackBankSnapshot& snapshot, DrumTrackOwners& out
) {
    DrumTrackOwners next;
    for (uint8_t i = 0; i < next.size(); ++i) {
        if ((snapshot.drumTrackMask & (1U << i)) == 0U) continue;
        next[i] = core::app::makeExtmemUniqueCopy(snapshot.tracks[i]);
        if (!next[i]) return false;
    }
    out = std::move(next);
    return true;
}

FLASHMEM void SequencerTrackBankState::installDrumTracks(
    DrumTrackOwners owners
) noexcept {
    drum_tracks_ = std::move(owners);
    drum_track_mask_ = 0U;
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        if (drum_tracks_[i]) drum_track_mask_ |= static_cast<uint16_t>(1U << i);
        drum_track_revisions_[i] = nextRevision(drum_track_revisions_[i]);
    }
    drum_revision_.set(nextRevision(drum_revision_.get()));
}

FLASHMEM void SequencerTrackBankState::clearDrumTrackBank() {
    installDrumTracks({});
}

FLASHMEM void SequencerTrackBankState::reset() {
    syncSharedTrackState(0x0001, 0);
    project_scale_settings_ = defaultProjectScaleSettings();
    project_scale_revision_.set(0);

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        auto& seq = tracks_[i];
        seq.reset();
        clips_[i].reset();
    }
    clearDrumTrackBank();
}

}  // namespace core::state::sequencer
