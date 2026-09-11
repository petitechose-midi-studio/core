#include "state/project/ProjectTrackHistory.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::state::project {

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(
    sizeof(ProjectTrackHistoryService) <= 4096U,
    "Track history must remain a compact PSRAM allocation"
);
#endif

namespace {

FLASHMEM bool liveMatches(
    const ProjectTrackState& state,
    const ProjectTrackSnapshot& snapshot
) {
    return sameProjectTrackSnapshot(state.authored, snapshot);
}

}  // namespace

FLASHMEM bool ProjectTrackHistoryService::record_(
    const ProjectTrackSnapshot& before,
    const ProjectTrackSnapshot& after,
    ProjectTrackHistoryActionKind kind,
    uint8_t trackIndex
) {
    if (pending_gesture_.active || !validProjectTrackIndex(trackIndex) ||
        !validProjectTrackSnapshot(before) ||
        !validProjectTrackSnapshot(after) ||
        sameProjectTrackSnapshot(before, after)) {
        return false;
    }

    return slots_.record(static_cast<uint8_t>(kind), project_history_sink_,
        [&](ProjectTrackHistoryEntry& entry) noexcept {
            entry.before = before;
            entry.after = after;
            entry.kind = kind;
            entry.trackIndex = trackIndex;
        });
}

FLASHMEM bool ProjectTrackHistoryService::beginGesture(
    const ProjectTrackState& state,
    ProjectTrackHistoryActionKind kind,
    uint8_t trackIndex
) {
    if (pending_gesture_.active || !validProjectTrackIndex(trackIndex)) {
        return false;
    }
    captureProjectTrackSnapshot(state, pending_gesture_.before);
    pending_gesture_.kind = kind;
    pending_gesture_.trackIndex = trackIndex;
    pending_gesture_.active = true;
    return true;
}

FLASHMEM bool ProjectTrackHistoryService::gestureMatches(
    ProjectTrackHistoryActionKind kind,
    uint8_t trackIndex
) const {
    return pending_gesture_.active &&
           pending_gesture_.kind == kind &&
           pending_gesture_.trackIndex == trackIndex;
}

FLASHMEM bool ProjectTrackHistoryService::commitGesture(
    ProjectTrackState& state
) {
    if (!pending_gesture_.active) return false;
    ProjectTrackSnapshot after{};
    captureProjectTrackSnapshot(state, after);
    const PendingGesture pending = pending_gesture_;
    pending_gesture_ = {};
    if (sameProjectTrackSnapshot(pending.before, after)) return false;
    if (record_(
            pending.before,
            after,
            pending.kind,
            pending.trackIndex
        )) {
        return true;
    }

    // Admission is deterministic, but remain atomic if state corruption ever
    // makes a pending command invalid.
    (void)applyProjectTrackSnapshot(state, pending.before);
    return false;
}

FLASHMEM bool ProjectTrackHistoryService::cancelGesture(
    ProjectTrackState& state
) {
    if (!pending_gesture_.active) return false;
    const ProjectTrackSnapshot before = pending_gesture_.before;
    pending_gesture_ = {};
    if (sameProjectTrackSnapshot(state.authored, before)) return false;
    return applyProjectTrackSnapshot(state, before).changed();
}

FLASHMEM bool ProjectTrackHistoryService::undo(ProjectTrackState& state) {
    return apply_(state, ProjectHistoryDirection::Undo);
}

FLASHMEM bool ProjectTrackHistoryService::redo(ProjectTrackState& state) {
    return apply_(state, ProjectHistoryDirection::Redo);
}

FLASHMEM bool ProjectTrackHistoryService::apply_(
    ProjectTrackState& state, ProjectHistoryDirection direction
) {
    if (pending_gesture_.active) return false;
    return slots_.apply(direction, project_history_sink_,
        [&](const ProjectTrackHistoryEntry& entry) {
            const bool undo = direction == ProjectHistoryDirection::Undo;
            return liveMatches(state, undo ? entry.after : entry.before) &&
                   applyProjectTrackSnapshot(state, undo ? entry.before : entry.after).changed();
        });
}

FLASHMEM void ProjectTrackHistoryService::clear() {
    slots_.clear(project_history_sink_);
    pending_gesture_ = {};
}

FLASHMEM void ProjectTrackHistoryService::discardRedoBranch() {
    slots_.discardRedo(project_history_sink_);
}

FLASHMEM uintptr_t ProjectTrackHistoryService::projectHistoryUndoIdentity() const {
    return slots_.identity(ProjectHistoryDirection::Undo);
}

FLASHMEM uintptr_t ProjectTrackHistoryService::projectHistoryRedoIdentity() const {
    return slots_.identity(ProjectHistoryDirection::Redo);
}

FLASHMEM const ProjectTrackHistoryEntry*
ProjectTrackHistoryService::peekUndo() const {
    return slots_.peek(ProjectHistoryDirection::Undo);
}

FLASHMEM const ProjectTrackHistoryEntry*
ProjectTrackHistoryService::peekRedo() const {
    return slots_.peek(ProjectHistoryDirection::Redo);
}

}  // namespace core::state::project
