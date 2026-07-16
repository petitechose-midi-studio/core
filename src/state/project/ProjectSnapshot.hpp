#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/modulation/ProjectControlDomainState.hpp"
#include "state/project/ProjectState.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::project {

struct ProjectSnapshot {
    ProjectState project{};
    std::array<core::state::macro::MacroTrackData, core::state::macro::TRACK_COUNT>
        macroTracks{};
    uint16_t sharedTrackEnabledMask = core::state::macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    uint8_t sharedTrackActive = 0;
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > projectControl;
    core::state::sequencer::SequencerHistoryTrackBankSnapshot sequencer{};

    ProjectSnapshot();
    ~ProjectSnapshot();
    ProjectSnapshot(const ProjectSnapshot&) = delete;
    ProjectSnapshot& operator=(const ProjectSnapshot&) = delete;
    ProjectSnapshot(ProjectSnapshot&&) noexcept;
    ProjectSnapshot& operator=(ProjectSnapshot&&) noexcept;
};

using ProjectSnapshotPtr = core::app::ExtmemUniquePtr<ProjectSnapshot>;

class ProjectSnapshotCapture {
public:
    enum class Status : uint8_t {
        IDLE = 0,
        IN_PROGRESS,
        COMPLETE,
        STALE,
        FAILED,
    };

    struct Progress {
        Status status = Status::IDLE;
        uint32_t modifiedCounter = 0;
    };

    bool begin(const core::state::CoreState& state, ProjectSnapshot& snapshot);
    Progress advance();
    void cancel();

    bool active() const;

private:
    enum class Phase : uint8_t {
        IDLE = 0,
        PROJECT,
        MACROS,
        AUTOMATION,
        SEQUENCER,
        COMPLETE,
    };

    const core::state::CoreState* state_ = nullptr;
    ProjectSnapshot* snapshot_ = nullptr;
    Phase phase_ = Phase::IDLE;
    uint32_t modified_counter_ = 0;

};

ProjectSnapshotPtr makeProjectSnapshot();
ProjectSnapshotPtr captureProjectSnapshotOwned(const core::state::CoreState& state);
bool captureProjectSnapshot(const core::state::CoreState& state, ProjectSnapshot& out);
bool applyProjectSnapshot(core::state::CoreState& state, const ProjectSnapshot& snapshot);

}  // namespace core::state::project
