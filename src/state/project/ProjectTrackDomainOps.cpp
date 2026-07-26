#include "state/project/ProjectTrackDomainOps.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {

namespace {

FLASHMEM ProjectTrackMutationResult result(
    ProjectTrackMutationStatus status,
    uint8_t track = PROJECT_TRACK_COUNT
) {
    return {
        .status = status,
        .trackIndex = track,
    };
}

FLASHMEM void bumpRevision(ProjectTrackState& state) {
    uint32_t next = state.revision.get() + 1U;
    if (next == 0U) next = 1U;
    state.revision.set(next);
}

FLASHMEM uint16_t trackBit(uint8_t track) {
    return static_cast<uint16_t>(1U << track);
}

FLASHMEM ProjectTrackMutationResult assignMask(
    ProjectTrackState& state,
    uint16_t& target,
    uint16_t value
) {
    if (target == value) {
        return result(ProjectTrackMutationStatus::NO_CHANGE);
    }
    target = value;
    bumpRevision(state);
    return result(ProjectTrackMutationStatus::OK);
}

}  // namespace

FLASHMEM bool validProjectTrackSnapshot(
    const ProjectTrackSnapshot& snapshot
) {
    for (uint8_t track = 0U; track < PROJECT_TRACK_COUNT; ++track) {
        if (!validProjectTrackMidiChannel(snapshot.midiChannels[track]) ||
            !validProjectTrackDelayMs(snapshot.delayMs[track])) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sameProjectTrackSnapshot(
    const ProjectTrackSnapshot& lhs,
    const ProjectTrackSnapshot& rhs
) {
    return lhs.delayMs == rhs.delayMs &&
           lhs.midiChannels == rhs.midiChannels &&
           lhs.mutedMask == rhs.mutedMask &&
           lhs.soloMask == rhs.soloMask;
}

FLASHMEM uint8_t projectTrackMidiChannel(
    const ProjectTrackState& state,
    uint8_t track
) {
    return validProjectTrackIndex(track)
        ? state.authored.midiChannels[track]
        : 0U;
}

FLASHMEM int16_t projectTrackDelayMs(
    const ProjectTrackState& state,
    uint8_t track
) {
    return validProjectTrackIndex(track)
        ? state.authored.delayMs[track]
        : 0;
}

FLASHMEM bool projectTrackMuted(
    const ProjectTrackState& state,
    uint8_t track
) {
    return validProjectTrackIndex(track) &&
           (state.authored.mutedMask & trackBit(track)) != 0U;
}

FLASHMEM bool projectTrackSoloed(
    const ProjectTrackState& state,
    uint8_t track
) {
    return validProjectTrackIndex(track) &&
           (state.authored.soloMask & trackBit(track)) != 0U;
}

FLASHMEM ProjectTrackMutationResult setProjectTrackMidiChannel(
    ProjectTrackState& state,
    uint8_t track,
    uint8_t channel0Based
) {
    if (!validProjectTrackIndex(track)) {
        return result(ProjectTrackMutationStatus::INVALID_TRACK, track);
    }
    if (!validProjectTrackMidiChannel(channel0Based)) {
        return result(
            ProjectTrackMutationStatus::INVALID_MIDI_CHANNEL,
            track
        );
    }
    if (state.authored.midiChannels[track] == channel0Based) {
        return result(ProjectTrackMutationStatus::NO_CHANGE, track);
    }

    state.authored.midiChannels[track] = channel0Based;
    bumpRevision(state);
    return result(ProjectTrackMutationStatus::OK, track);
}

FLASHMEM ProjectTrackMutationResult setProjectTrackDelayMs(
    ProjectTrackState& state,
    uint8_t track,
    int32_t delayMs
) {
    if (!validProjectTrackIndex(track)) {
        return result(ProjectTrackMutationStatus::INVALID_TRACK, track);
    }
    if (!validProjectTrackDelayMs(delayMs)) {
        return result(ProjectTrackMutationStatus::INVALID_DELAY, track);
    }
    const auto value = static_cast<int16_t>(delayMs);
    if (state.authored.delayMs[track] == value) {
        return result(ProjectTrackMutationStatus::NO_CHANGE, track);
    }

    state.authored.delayMs[track] = value;
    bumpRevision(state);
    return result(ProjectTrackMutationStatus::OK, track);
}

FLASHMEM ProjectTrackMutationResult setProjectTrackMuted(
    ProjectTrackState& state,
    uint8_t track,
    bool muted
) {
    if (!validProjectTrackIndex(track)) {
        return result(ProjectTrackMutationStatus::INVALID_TRACK, track);
    }
    const uint16_t bit = trackBit(track);
    const uint16_t next = muted
        ? static_cast<uint16_t>(state.authored.mutedMask | bit)
        : static_cast<uint16_t>(state.authored.mutedMask &
                                static_cast<uint16_t>(~bit));
    if (next == state.authored.mutedMask) {
        return result(ProjectTrackMutationStatus::NO_CHANGE, track);
    }

    state.authored.mutedMask = next;
    bumpRevision(state);
    return result(ProjectTrackMutationStatus::OK, track);
}

FLASHMEM ProjectTrackMutationResult setProjectTrackSoloed(
    ProjectTrackState& state,
    uint8_t track,
    bool soloed
) {
    if (!validProjectTrackIndex(track)) {
        return result(ProjectTrackMutationStatus::INVALID_TRACK, track);
    }
    const uint16_t bit = trackBit(track);
    const uint16_t next = soloed
        ? static_cast<uint16_t>(state.authored.soloMask | bit)
        : static_cast<uint16_t>(state.authored.soloMask &
                                static_cast<uint16_t>(~bit));
    if (next == state.authored.soloMask) {
        return result(ProjectTrackMutationStatus::NO_CHANGE, track);
    }

    state.authored.soloMask = next;
    bumpRevision(state);
    return result(ProjectTrackMutationStatus::OK, track);
}

FLASHMEM ProjectTrackMutationResult setProjectTrackMutedMask(
    ProjectTrackState& state,
    uint16_t mutedMask
) {
    return assignMask(state, state.authored.mutedMask, mutedMask);
}

FLASHMEM ProjectTrackMutationResult setProjectTrackSoloMask(
    ProjectTrackState& state,
    uint16_t soloMask
) {
    return assignMask(state, state.authored.soloMask, soloMask);
}

FLASHMEM void captureProjectTrackSnapshot(
    const ProjectTrackState& state,
    ProjectTrackSnapshot& out
) {
    out = state.authored;
}

FLASHMEM ProjectTrackMutationResult applyProjectTrackSnapshot(
    ProjectTrackState& state,
    const ProjectTrackSnapshot& snapshot
) {
    if (!validProjectTrackSnapshot(snapshot)) {
        return result(ProjectTrackMutationStatus::INVALID_SNAPSHOT);
    }
    if (sameProjectTrackSnapshot(state.authored, snapshot)) {
        return result(ProjectTrackMutationStatus::NO_CHANGE);
    }

    state.authored = snapshot;
    bumpRevision(state);
    return result(ProjectTrackMutationStatus::OK);
}

FLASHMEM ProjectTrackMutationResult resetProjectTracks(
    ProjectTrackState& state
) {
    return applyProjectTrackSnapshot(state, defaultProjectTrackSnapshot());
}

FLASHMEM uint16_t audibleMask(
    const ProjectTrackState& state,
    uint16_t enabledMask
) {
    const uint16_t selected = state.authored.soloMask != 0U
        ? state.authored.soloMask
        : PROJECT_TRACK_ALL_MASK;
    return static_cast<uint16_t>(
        enabledMask &
        static_cast<uint16_t>(~state.authored.mutedMask) &
        selected
    );
}

}  // namespace core::state::project
