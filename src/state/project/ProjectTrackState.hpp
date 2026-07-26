#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include <oc/state/Signal.hpp>

namespace core::state::project {

inline constexpr uint8_t PROJECT_TRACK_COUNT = 16U;
inline constexpr uint16_t PROJECT_TRACK_ALL_MASK = 0xFFFFU;
inline constexpr uint8_t PROJECT_TRACK_MIDI_CHANNEL_MAX_0BASED = 15U;
inline constexpr int16_t PROJECT_TRACK_DELAY_MIN_MS = -100;
inline constexpr int16_t PROJECT_TRACK_DELAY_MAX_MS = 100;

/**
 * Compact authored Track routing/mix state used at history and persistence
 * boundaries.
 *
 * MIDI Channels deliberately use the existing internal 0..15 convention and
 * are presented to musicians as 1..16. USB is a fixed V1 product route and is
 * therefore not stored here. Track enabled/occupied state is owned by the
 * shared structure domain, not by this snapshot.
 */
struct ProjectTrackSnapshot {
    std::array<int16_t, PROJECT_TRACK_COUNT> delayMs{};
    std::array<uint8_t, PROJECT_TRACK_COUNT> midiChannels{};
    uint16_t mutedMask = 0U;
    uint16_t soloMask = 0U;
};

static_assert(sizeof(ProjectTrackSnapshot) == 52U);
static_assert(std::is_trivially_copyable_v<ProjectTrackSnapshot>);

[[nodiscard]] constexpr ProjectTrackSnapshot defaultProjectTrackSnapshot() {
    ProjectTrackSnapshot snapshot{};
    for (uint8_t track = 0U; track < PROJECT_TRACK_COUNT; ++track) {
        snapshot.midiChannels[track] = track;
    }
    return snapshot;
}

/**
 * Project-owned Track control aggregate.
 *
 * Authored values remain plain bounded storage; every public mutation goes
 * through ProjectTrackDomainOps and publishes at most one aggregate revision.
 * Four subscriber slots cover persistence, runtime projection and the two UI
 * surfaces without multiplying one Signal per Track/property.
 */
struct ProjectTrackState {
    static constexpr uint8_t REVISION_MAX_SUBSCRIBERS = 4U;
    using RevisionSignal =
        oc::state::Signal<uint32_t, REVISION_MAX_SUBSCRIBERS>;

    ProjectTrackSnapshot authored{};
    RevisionSignal revision{0U};

    ProjectTrackState();

    /** Lifecycle reset. Runtime/editor mutations must use DomainOps. */
    void reset();
};

}  // namespace core::state::project
