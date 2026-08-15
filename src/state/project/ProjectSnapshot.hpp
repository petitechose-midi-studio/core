#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/modulation/ProjectControlDomainState.hpp"
#include "state/project/ProjectSaveToken.hpp"
#include "state/project/ProjectState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::project {

struct ProjectSnapshot {
    ProjectState project{};
    ProjectTrackSnapshot projectTracks = defaultProjectTrackSnapshot();
    std::array<core::state::macro::MacroTrackData, core::state::macro::TRACK_COUNT>
        macroTracks{};
    uint16_t sharedTrackEnabledMask = core::state::macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    uint8_t sharedTrackActive = 0;
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > projectControl;
    core::state::sequencer::SequencerHistoryTrackBankSnapshot sequencer{};
    core::app::ExtmemUniquePtr<
        core::state::sequencer::DrumTrackBankSnapshot
    > drumTracks;

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

    enum class SliceKind : uint8_t {
        SMALL = 0,
        SEQUENCER,
    };

    struct Progress {
        Status status = Status::IDLE;
        uint32_t modifiedCounter = 0;
        uint32_t workBytes = 0;
    };

    bool begin(const core::state::CoreState& state, ProjectSnapshot& snapshot);
    Progress advance();
    void cancel();

    bool active() const;
    bool complete() const;
    const ProjectCaptureGuard* guard() const;
    SliceKind nextSliceKind() const;

private:
    enum class BoundaryMode : uint8_t {
        // Yielded captures require every Project transaction to be closed.
        COOPERATIVE_QUIESCENT = 0,
        // Non-yielding compatibility captures may observe live coalesced edits,
        // but boundaryReady_ still rejects rollback-capable transactions.
        SYNCHRONOUS_CURRENT_STATE,
    };

    enum class Phase : uint8_t {
        IDLE = 0,
        PROJECT,
        MACROS,
        AUTOMATION,
        SEQUENCER_GRAPH,
        SEQUENCER_DATA,
        COMPLETE,
    };

    static bool boundaryReady_(
        const core::state::CoreState& state,
        BoundaryMode mode
    );
    bool begin_(
        const core::state::CoreState& state,
        ProjectSnapshot& snapshot,
        BoundaryMode mode
    );
    static bool captureSynchronously_(
        const core::state::CoreState& state,
        ProjectSnapshot& snapshot
    );
    bool guardMatches_() const;

    friend ProjectSnapshotPtr captureProjectSnapshotOwned(
        const core::state::CoreState& state
    );
    friend bool captureProjectSnapshot(
        const core::state::CoreState& state,
        ProjectSnapshot& out
    );

    const core::state::CoreState* state_ = nullptr;
    ProjectSnapshot* snapshot_ = nullptr;
    Phase phase_ = Phase::IDLE;
    BoundaryMode mode_ = BoundaryMode::COOPERATIVE_QUIESCENT;
    ProjectCaptureGuard guard_{};
    uint32_t automation_offset_ = 0U;
    uint8_t macro_track_ = 0U;
    uint8_t sequencer_track_ = 0U;
    uint8_t frozen_active_track_ = 0U;
    uint16_t frozen_drum_track_mask_ = 0U;
    uint8_t frozen_focused_step_ = 0U;
    core::state::sequencer::StepProperty frozen_active_step_property_ =
        core::state::sequencer::StepProperty::NOTE;
};

ProjectSnapshotPtr makeProjectSnapshot();

ProjectSnapshotPtr captureProjectSnapshotOwned(const core::state::CoreState& state);
bool captureProjectSnapshot(const core::state::CoreState& state, ProjectSnapshot& out);
bool applyProjectSnapshot(core::state::CoreState& state, const ProjectSnapshot& snapshot);

}  // namespace core::state::project
