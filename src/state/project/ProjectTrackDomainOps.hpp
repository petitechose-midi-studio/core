#pragma once

#include <cstdint>

#include "state/project/ProjectTrackState.hpp"

namespace core::state::project {

enum class ProjectTrackMutationStatus : uint8_t {
    OK = 0,
    NO_CHANGE,
    INVALID_TRACK,
    INVALID_MIDI_CHANNEL,
    INVALID_DELAY,
    INVALID_SNAPSHOT,
};

struct ProjectTrackMutationResult {
    ProjectTrackMutationStatus status =
        ProjectTrackMutationStatus::INVALID_TRACK;
    uint8_t trackIndex = PROJECT_TRACK_COUNT;

    [[nodiscard]] bool changed() const {
        return status == ProjectTrackMutationStatus::OK;
    }
};

[[nodiscard]] constexpr bool validProjectTrackIndex(uint8_t track) {
    return track < PROJECT_TRACK_COUNT;
}

/** Internal representation is 0..15; user-facing presentation is 1..16. */
[[nodiscard]] constexpr bool validProjectTrackMidiChannel(
    uint8_t channel0Based
) {
    return channel0Based <= PROJECT_TRACK_MIDI_CHANNEL_MAX_0BASED;
}

[[nodiscard]] constexpr bool validProjectTrackDelayMs(int32_t delayMs) {
    return delayMs >= PROJECT_TRACK_DELAY_MIN_MS &&
           delayMs <= PROJECT_TRACK_DELAY_MAX_MS;
}

[[nodiscard]] bool validProjectTrackSnapshot(
    const ProjectTrackSnapshot& snapshot
);
[[nodiscard]] bool sameProjectTrackSnapshot(
    const ProjectTrackSnapshot& lhs,
    const ProjectTrackSnapshot& rhs
);

[[nodiscard]] uint8_t projectTrackMidiChannel(
    const ProjectTrackState& state,
    uint8_t track
);
[[nodiscard]] int16_t projectTrackDelayMs(
    const ProjectTrackState& state,
    uint8_t track
);
[[nodiscard]] bool projectTrackMuted(
    const ProjectTrackState& state,
    uint8_t track
);
[[nodiscard]] bool projectTrackSoloed(
    const ProjectTrackState& state,
    uint8_t track
);

ProjectTrackMutationResult setProjectTrackMidiChannel(
    ProjectTrackState& state,
    uint8_t track,
    uint8_t channel0Based
);
ProjectTrackMutationResult setProjectTrackDelayMs(
    ProjectTrackState& state,
    uint8_t track,
    int32_t delayMs
);
ProjectTrackMutationResult setProjectTrackMuted(
    ProjectTrackState& state,
    uint8_t track,
    bool muted
);
ProjectTrackMutationResult setProjectTrackSoloed(
    ProjectTrackState& state,
    uint8_t track,
    bool soloed
);
ProjectTrackMutationResult setProjectTrackMutedMask(
    ProjectTrackState& state,
    uint16_t mutedMask
);
ProjectTrackMutationResult setProjectTrackSoloMask(
    ProjectTrackState& state,
    uint16_t soloMask
);

void captureProjectTrackSnapshot(
    const ProjectTrackState& state,
    ProjectTrackSnapshot& out
);

/** Strict and atomic; malformed snapshots never partially publish. */
ProjectTrackMutationResult applyProjectTrackSnapshot(
    ProjectTrackState& state,
    const ProjectTrackSnapshot& snapshot
);

/** Applies canonical defaults through the same one-revision transaction. */
ProjectTrackMutationResult resetProjectTracks(ProjectTrackState& state);

/**
 * Resolves audible Tracks without taking ownership of structural enablement.
 * Mute always wins. Any authored Solo mask selects Soloed Tracks globally.
 */
[[nodiscard]] uint16_t audibleMask(
    const ProjectTrackState& state,
    uint16_t enabledMask
);

}  // namespace core::state::project
