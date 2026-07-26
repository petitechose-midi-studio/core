#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/project/ProjectTrackState.hpp"

namespace core::sequencer {

/**
 * Compact immutable Track routing/mix projection consumed by realtime code.
 *
 * Authored state remains Project-owned in PSRAM. Two instances of this POD are
 * intended to live inline with the RAM2 realtime lane and share the existing
 * Sequencer snapshot index. Delay remains in milliseconds so the complete
 * double buffer stays within the 128-byte G10 budget.
 */
struct ProjectTrackRuntimeSnapshot {
    uint32_t revision = 0U;
    std::array<int16_t, core::state::project::PROJECT_TRACK_COUNT> delayMs{};
    std::array<uint8_t, core::state::project::PROJECT_TRACK_COUNT>
        midiChannels{};
    uint16_t enabledMask = 0U;
    uint16_t mutedMask = 0U;
    uint16_t soloMask = 0U;
    uint16_t audibleMask = 0U;
};

static_assert(sizeof(ProjectTrackRuntimeSnapshot) == 60U);
static_assert(std::is_trivially_copyable_v<ProjectTrackRuntimeSnapshot>);
static_assert(std::is_standard_layout_v<ProjectTrackRuntimeSnapshot>);

void captureProjectTrackRuntimeSnapshot(
    const core::state::project::ProjectTrackState& source,
    uint16_t enabledMask,
    ProjectTrackRuntimeSnapshot& out
);

/**
 * Allocation-free two-slot publication bank.
 *
 * This type deliberately owns no active index: the future runtime integration
 * publishes slot 0/1 with the already-atomic Sequencer snapshot index, avoiding
 * a second generation or commit authority.
 */
class ProjectTrackRuntimeSnapshotBank final {
public:
    static constexpr uint8_t SLOT_COUNT = 2U;

    [[nodiscard]] bool publish(
        uint8_t slot,
        const core::state::project::ProjectTrackState& source,
        uint16_t enabledMask
    );

    [[nodiscard]] const ProjectTrackRuntimeSnapshot* snapshot(
        uint8_t slot
    ) const;

private:
    std::array<ProjectTrackRuntimeSnapshot, SLOT_COUNT> slots_{};
};

static_assert(sizeof(ProjectTrackRuntimeSnapshotBank) == 120U);
static_assert(std::is_standard_layout_v<ProjectTrackRuntimeSnapshotBank>);

}  // namespace core::sequencer
