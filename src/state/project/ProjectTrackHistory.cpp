#include "state/project/ProjectTrackHistory.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::state::project {

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(
    sizeof(ProjectTrackHistoryService) <= 1024U,
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

    // A new authored command cuts the complete local Redo branch before its
    // identity is published to the global chronology.
    clearRedo_();
    uint8_t slot = acquireSlot_();
    if (slot == INVALID_SLOT) {
        evictOldestUndo_();
        slot = acquireSlot_();
    }
    if (slot == INVALID_SLOT || undo_count_ >= ENTRY_LIMIT) return false;

    entries_[slot] = ProjectTrackHistoryEntry{
        .before = before,
        .after = after,
        .kind = kind,
        .trackIndex = trackIndex,
        .occupied = true,
    };
    undo_slots_[undo_count_++] = slot;

    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCommitted(
            ProjectHistoryDomain::Track,
            identity_(slot),
            static_cast<uint8_t>(kind)
        );
    }
    return true;
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
    if (pending_gesture_.active || undo_count_ == 0U ||
        redo_count_ >= ENTRY_LIMIT) return false;
    const uint8_t slot = undo_slots_[undo_count_ - 1U];
    if (slot >= ENTRY_LIMIT || !entries_[slot].occupied) return false;
    auto& entry = entries_[slot];
    if (!liveMatches(state, entry.after) ||
        !applyProjectTrackSnapshot(state, entry.before).changed()) {
        return false;
    }

    --undo_count_;
    undo_slots_[undo_count_] = INVALID_SLOT;
    redo_slots_[redo_count_++] = slot;
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            ProjectHistoryDomain::Track,
            identity_(slot),
            ProjectHistoryDirection::Undo
        );
    }
    return true;
}

FLASHMEM bool ProjectTrackHistoryService::redo(ProjectTrackState& state) {
    if (pending_gesture_.active || redo_count_ == 0U ||
        undo_count_ >= ENTRY_LIMIT) return false;
    const uint8_t slot = redo_slots_[redo_count_ - 1U];
    if (slot >= ENTRY_LIMIT || !entries_[slot].occupied) return false;
    auto& entry = entries_[slot];
    if (!liveMatches(state, entry.before) ||
        !applyProjectTrackSnapshot(state, entry.after).changed()) {
        return false;
    }

    --redo_count_;
    redo_slots_[redo_count_] = INVALID_SLOT;
    undo_slots_[undo_count_++] = slot;
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            ProjectHistoryDomain::Track,
            identity_(slot),
            ProjectHistoryDirection::Redo
        );
    }
    return true;
}

FLASHMEM void ProjectTrackHistoryService::clear() {
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCleared(ProjectHistoryDomain::Track);
    }
    entries_ = {};
    undo_slots_.fill(INVALID_SLOT);
    redo_slots_.fill(INVALID_SLOT);
    undo_count_ = 0U;
    redo_count_ = 0U;
    pending_gesture_ = {};
}

FLASHMEM void ProjectTrackHistoryService::discardRedoBranch() {
    clearRedo_();
}

FLASHMEM uintptr_t ProjectTrackHistoryService::projectHistoryUndoIdentity() const {
    return undo_count_ == 0U
        ? 0U
        : identity_(undo_slots_[undo_count_ - 1U]);
}

FLASHMEM uintptr_t ProjectTrackHistoryService::projectHistoryRedoIdentity() const {
    return redo_count_ == 0U
        ? 0U
        : identity_(redo_slots_[redo_count_ - 1U]);
}

FLASHMEM const ProjectTrackHistoryEntry*
ProjectTrackHistoryService::peekUndo() const {
    if (undo_count_ == 0U) return nullptr;
    const uint8_t slot = undo_slots_[undo_count_ - 1U];
    return slot < ENTRY_LIMIT && entries_[slot].occupied
        ? &entries_[slot]
        : nullptr;
}

FLASHMEM const ProjectTrackHistoryEntry*
ProjectTrackHistoryService::peekRedo() const {
    if (redo_count_ == 0U) return nullptr;
    const uint8_t slot = redo_slots_[redo_count_ - 1U];
    return slot < ENTRY_LIMIT && entries_[slot].occupied
        ? &entries_[slot]
        : nullptr;
}

FLASHMEM uint8_t ProjectTrackHistoryService::acquireSlot_() {
    for (uint8_t slot = 0U; slot < ENTRY_LIMIT; ++slot) {
        if (!entries_[slot].occupied) return slot;
    }
    return INVALID_SLOT;
}

FLASHMEM void ProjectTrackHistoryService::releaseSlot_(uint8_t slot) {
    if (slot >= ENTRY_LIMIT) return;
    entries_[slot] = {};
}

FLASHMEM void ProjectTrackHistoryService::evictOldestUndo_() {
    if (undo_count_ == 0U) return;
    const uint8_t slot = undo_slots_[0];
    if (project_history_sink_ != nullptr && slot < ENTRY_LIMIT) {
        project_history_sink_->notifyEvicted(
            ProjectHistoryDomain::Track,
            identity_(slot)
        );
    }
    for (uint8_t index = 1U; index < undo_count_; ++index) {
        undo_slots_[index - 1U] = undo_slots_[index];
    }
    --undo_count_;
    undo_slots_[undo_count_] = INVALID_SLOT;
    releaseSlot_(slot);
}

FLASHMEM void ProjectTrackHistoryService::clearRedo_() {
    for (uint8_t index = 0U; index < redo_count_; ++index) {
        const uint8_t slot = redo_slots_[index];
        if (project_history_sink_ != nullptr && slot < ENTRY_LIMIT) {
            project_history_sink_->notifyEvicted(
                ProjectHistoryDomain::Track,
                identity_(slot)
            );
        }
        releaseSlot_(slot);
        redo_slots_[index] = INVALID_SLOT;
    }
    redo_count_ = 0U;
}

FLASHMEM uintptr_t ProjectTrackHistoryService::identity_(uint8_t slot) const {
    return slot < ENTRY_LIMIT
        ? reinterpret_cast<uintptr_t>(&entries_[slot])
        : 0U;
}

}  // namespace core::state::project
