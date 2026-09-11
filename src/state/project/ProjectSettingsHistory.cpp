#include "state/project/ProjectSettingsHistory.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectDomainRules.hpp"

namespace core::state::project {

namespace {

constexpr uint8_t PATTERNS_INHERIT_SCALE = 0x01U;
constexpr uint8_t CLIPS_INHERIT_SCALE = 0x02U;

}  // namespace

FLASHMEM ProjectSettingsHistorySnapshot captureProjectSettingsHistorySnapshot(
    const StatusBarState& statusBar,
    const ProjectNavigationState& navigation
) {
    ProjectSettingsHistorySnapshot out{};
    out.tempoBpm = sanitizeProjectTempoBpm(statusBar.tempo.get());
    out.stepPasteMode = sanitizeProjectStepPasteMode(
        static_cast<uint8_t>(navigation.stepPasteMode)
    );
    for (uint8_t lane = 0U; lane < PROJECT_CC_LANE_DEFAULT_COUNT; ++lane) {
        out.ccLaneDefaultControllers[lane] = sanitizeProjectCcLaneDefault(
            navigation.ccLaneDefaultControllers[lane],
            lane
        );
    }
    out.swingPercent = sanitizeProjectSwingPercent(
        navigation.transportSwingPercent
    );
    out.runMode = sanitizeProjectRunMode(navigation.transportRunMode);
    out.scaleInheritanceFlags =
        (navigation.patternsInheritScale ? PATTERNS_INHERIT_SCALE : 0U) |
        (navigation.clipsInheritScale ? CLIPS_INHERIT_SCALE : 0U);
    return out;
}

FLASHMEM bool sameProjectSettingsHistorySnapshot(
    const ProjectSettingsHistorySnapshot& lhs,
    const ProjectSettingsHistorySnapshot& rhs
) {
    return lhs.tempoBpm == rhs.tempoBpm &&
           lhs.stepPasteMode == rhs.stepPasteMode &&
           lhs.ccLaneDefaultControllers == rhs.ccLaneDefaultControllers &&
           lhs.swingPercent == rhs.swingPercent &&
           lhs.runMode == rhs.runMode &&
           lhs.scaleInheritanceFlags == rhs.scaleInheritanceFlags;
}

FLASHMEM bool applyProjectSettingsHistorySnapshot(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation,
    const ProjectSettingsHistorySnapshot& snapshot
) {
    const auto before = captureProjectSettingsHistorySnapshot(
        statusBar,
        navigation
    );
    const float tempo = sanitizeProjectTempoBpm(snapshot.tempoBpm);
    statusBar.tempo.set(tempo);
    if (!statusBar.tempoLocked.get()) statusBar.tempoDisplay.set(tempo);
    navigation.stepPasteMode = sanitizeProjectStepPasteMode(
        static_cast<uint8_t>(snapshot.stepPasteMode)
    );
    for (uint8_t lane = 0U; lane < PROJECT_CC_LANE_DEFAULT_COUNT; ++lane) {
        navigation.ccLaneDefaultControllers[lane] = sanitizeProjectCcLaneDefault(
            snapshot.ccLaneDefaultControllers[lane],
            lane
        );
    }
    navigation.transportSwingPercent = sanitizeProjectSwingPercent(
        snapshot.swingPercent
    );
    navigation.transportRunMode = sanitizeProjectRunMode(snapshot.runMode);
    navigation.patternsInheritScale =
        (snapshot.scaleInheritanceFlags & PATTERNS_INHERIT_SCALE) != 0U;
    navigation.clipsInheritScale =
        (snapshot.scaleInheritanceFlags & CLIPS_INHERIT_SCALE) != 0U;
    navigation.notifyContentChanged();
    return !sameProjectSettingsHistorySnapshot(
        before,
        captureProjectSettingsHistorySnapshot(statusBar, navigation)
    );
}

FLASHMEM bool ProjectSettingsHistoryService::record(
    const ProjectSettingsHistorySnapshot& before,
    const ProjectSettingsHistorySnapshot& after,
    ProjectSettingsHistoryActionKind kind,
    uint8_t subject,
    bool coalesce
) {
    if (sameProjectSettingsHistorySnapshot(before, after)) return false;

    if (coalesce && coalescing_ &&
        coalesced_kind_ == kind && coalesced_subject_ == subject) {
        auto* previous = slots_.peek(ProjectHistoryDirection::Undo);
        if (previous && sameProjectSettingsHistorySnapshot(previous->after, before)) {
            previous->after = after;
            slots_.discardRedo(project_history_sink_);
            return true;
        }
    }

    endCoalescing();
    if (!slots_.record(static_cast<uint8_t>(kind), project_history_sink_,
            [&](ProjectSettingsHistoryEntry& entry) noexcept {
                entry.before = before;
                entry.after = after;
                entry.kind = kind;
                entry.subject = subject;
            })) return false;
    coalescing_ = coalesce;
    coalesced_kind_ = kind;
    coalesced_subject_ = subject;
    return true;
}

FLASHMEM bool ProjectSettingsHistoryService::undo(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation
) {
    return apply_(statusBar, navigation, ProjectHistoryDirection::Undo);
}

FLASHMEM bool ProjectSettingsHistoryService::redo(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation
) {
    return apply_(statusBar, navigation, ProjectHistoryDirection::Redo);
}

FLASHMEM bool ProjectSettingsHistoryService::apply_(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation,
    ProjectHistoryDirection direction
) {
    endCoalescing();
    return slots_.apply(direction, project_history_sink_,
        [&](const ProjectSettingsHistoryEntry& entry) {
            const bool undo = direction == ProjectHistoryDirection::Undo;
            return sameProjectSettingsHistorySnapshot(
                       captureProjectSettingsHistorySnapshot(statusBar, navigation),
                       undo ? entry.after : entry.before) &&
                   applyProjectSettingsHistorySnapshot(
                       statusBar, navigation, undo ? entry.before : entry.after);
        });
}

FLASHMEM void ProjectSettingsHistoryService::clear() {
    slots_.clear(project_history_sink_);
    endCoalescing();
}

FLASHMEM void ProjectSettingsHistoryService::discardRedoBranch() {
    slots_.discardRedo(project_history_sink_);
}

FLASHMEM uintptr_t ProjectSettingsHistoryService::projectHistoryUndoIdentity() const {
    return slots_.identity(ProjectHistoryDirection::Undo);
}

FLASHMEM uintptr_t ProjectSettingsHistoryService::projectHistoryRedoIdentity() const {
    return slots_.identity(ProjectHistoryDirection::Redo);
}

}  // namespace core::state::project
