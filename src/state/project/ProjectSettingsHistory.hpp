#pragma once

#include <array>
#include <cstdint>

#include "state/StatusBarState.hpp"
#include "state/project/ProjectHistorySlots.hpp"
#include "state/project/ProjectNavigationState.hpp"

namespace core::state::project {

enum class ProjectSettingsHistoryActionKind : uint8_t {
    Tempo = 0,
    Swing,
    RunMode,
    StepPasteMode,
    CcLaneDefault,
    PatternsInheritScale,
    ClipsInheritScale,
};

struct ProjectSettingsHistorySnapshot {
    float tempoBpm = PROJECT_TEMPO_DEFAULT_BPM;
    ProjectStepPasteMode stepPasteMode = PROJECT_STEP_PASTE_MODE_DEFAULT;
    std::array<uint8_t, PROJECT_CC_LANE_DEFAULT_COUNT>
        ccLaneDefaultControllers = PROJECT_CC_LANE_DEFAULT_CONTROLLERS;
    uint8_t swingPercent = PROJECT_SWING_DEFAULT_PERCENT;
    uint8_t runMode = PROJECT_RUN_MODE_DEFAULT;
    uint8_t scaleInheritanceFlags = 0x03U;
};

struct ProjectSettingsHistoryEntry {
    ProjectSettingsHistorySnapshot before{};
    ProjectSettingsHistorySnapshot after{};
    ProjectSettingsHistoryActionKind kind =
        ProjectSettingsHistoryActionKind::Tempo;
    uint8_t subject = 0U;
    bool occupied = false;
};

static_assert(sizeof(ProjectSettingsHistorySnapshot) == 12U);
static_assert(sizeof(ProjectSettingsHistoryEntry) == 28U);

[[nodiscard]] ProjectSettingsHistorySnapshot captureProjectSettingsHistorySnapshot(
    const StatusBarState& statusBar,
    const ProjectNavigationState& navigation
);

[[nodiscard]] bool sameProjectSettingsHistorySnapshot(
    const ProjectSettingsHistorySnapshot& lhs,
    const ProjectSettingsHistorySnapshot& rhs
);

[[nodiscard]] bool applyProjectSettingsHistorySnapshot(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation,
    const ProjectSettingsHistorySnapshot& snapshot
);

class ProjectSettingsHistoryService {
public:
    static constexpr uint8_t ENTRY_LIMIT = 8U;

    void setProjectHistoryEventSink(const ProjectHistoryEventSink* sink) {
        project_history_sink_ = sink;
    }

    [[nodiscard]] bool record(
        const ProjectSettingsHistorySnapshot& before,
        const ProjectSettingsHistorySnapshot& after,
        ProjectSettingsHistoryActionKind kind,
        uint8_t subject,
        bool coalesce
    );
    void endCoalescing() { coalescing_ = false; }

    [[nodiscard]] bool undo(
        StatusBarState& statusBar,
        ProjectNavigationState& navigation
    );
    [[nodiscard]] bool redo(
        StatusBarState& statusBar,
        ProjectNavigationState& navigation
    );
    void clear();
    void discardRedoBranch();

    [[nodiscard]] uintptr_t projectHistoryUndoIdentity() const;
    [[nodiscard]] uintptr_t projectHistoryRedoIdentity() const;

private:
    [[nodiscard]] bool apply_(StatusBarState& statusBar,
                              ProjectNavigationState& navigation,
                              ProjectHistoryDirection direction);

    ProjectHistorySlots<ProjectSettingsHistoryEntry, ProjectHistoryDomain::Settings, ENTRY_LIMIT> slots_{};
    bool coalescing_ = false;
    ProjectSettingsHistoryActionKind coalesced_kind_ =
        ProjectSettingsHistoryActionKind::Tempo;
    uint8_t coalesced_subject_ = 0U;
    const ProjectHistoryEventSink* project_history_sink_ = nullptr;
};

}  // namespace core::state::project
