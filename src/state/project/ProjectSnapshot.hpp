#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"
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
    core::app::ExtmemUniquePtr<core::state::macro::MacroAutomationBankState> macroAutomation;
    core::state::sequencer::SequencerHistoryTrackBankSnapshot sequencer{};

    ProjectSnapshot();
    ~ProjectSnapshot();
    ProjectSnapshot(const ProjectSnapshot&) = delete;
    ProjectSnapshot& operator=(const ProjectSnapshot&) = delete;
    ProjectSnapshot(ProjectSnapshot&&) noexcept;
    ProjectSnapshot& operator=(ProjectSnapshot&&) noexcept;
};

bool captureProjectSnapshot(const core::state::CoreState& state, ProjectSnapshot& out);
bool applyProjectSnapshot(core::state::CoreState& state, const ProjectSnapshot& snapshot);

}  // namespace core::state::project
