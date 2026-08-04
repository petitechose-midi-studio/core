#include "state/project/ProjectSettingsHistory.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectDomainRules.hpp"

namespace core::state::project {

FLASHMEM ProjectSettingsHistorySnapshot captureProjectSettingsHistorySnapshot(
    const StatusBarState& statusBar,
    const ProjectNavigationState& navigation,
    const MidiSyncState& midiSync
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
    out.syncMode = midiSync.mode.get();
    out.swingPercent = sanitizeProjectSwingPercent(
        navigation.transportSwingPercent
    );
    out.runMode = sanitizeProjectRunMode(navigation.transportRunMode);
    out.patternsInheritScale = navigation.patternsInheritScale;
    out.clipsInheritScale = navigation.clipsInheritScale;
    return out;
}

FLASHMEM bool sameProjectSettingsHistorySnapshot(
    const ProjectSettingsHistorySnapshot& lhs,
    const ProjectSettingsHistorySnapshot& rhs
) {
    return lhs.tempoBpm == rhs.tempoBpm &&
           lhs.stepPasteMode == rhs.stepPasteMode &&
           lhs.ccLaneDefaultControllers == rhs.ccLaneDefaultControllers &&
           lhs.syncMode == rhs.syncMode &&
           lhs.swingPercent == rhs.swingPercent &&
           lhs.runMode == rhs.runMode &&
           lhs.patternsInheritScale == rhs.patternsInheritScale &&
           lhs.clipsInheritScale == rhs.clipsInheritScale;
}

FLASHMEM bool applyProjectSettingsHistorySnapshot(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation,
    MidiSyncState& midiSync,
    const ProjectSettingsHistorySnapshot& snapshot
) {
    const auto before = captureProjectSettingsHistorySnapshot(
        statusBar,
        navigation,
        midiSync
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
    midiSync.mode.set(snapshot.syncMode);
    navigation.transportSwingPercent = sanitizeProjectSwingPercent(
        snapshot.swingPercent
    );
    navigation.transportRunMode = sanitizeProjectRunMode(snapshot.runMode);
    navigation.patternsInheritScale = snapshot.patternsInheritScale;
    navigation.clipsInheritScale = snapshot.clipsInheritScale;
    navigation.notifyContentChanged();
    return !sameProjectSettingsHistorySnapshot(
        before,
        captureProjectSettingsHistorySnapshot(statusBar, navigation, midiSync)
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

    if (coalesce && coalescing_ && undo_count_ > 0U &&
        coalesced_kind_ == kind && coalesced_subject_ == subject) {
        const uint8_t slot = undo_slots_[undo_count_ - 1U];
        if (slot < ENTRY_LIMIT && entries_[slot].occupied &&
            sameProjectSettingsHistorySnapshot(entries_[slot].after, before)) {
            entries_[slot].after = after;
            clearRedo_();
            return true;
        }
    }

    endCoalescing();
    clearRedo_();
    uint8_t slot = acquireSlot_();
    if (slot == INVALID_SLOT) {
        evictOldestUndo_();
        slot = acquireSlot_();
    }
    if (slot == INVALID_SLOT || undo_count_ >= ENTRY_LIMIT) return false;
    entries_[slot] = ProjectSettingsHistoryEntry{
        .before = before,
        .after = after,
        .kind = kind,
        .subject = subject,
        .occupied = true,
    };
    undo_slots_[undo_count_++] = slot;
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCommitted(
            ProjectHistoryDomain::Settings,
            identity_(slot),
            static_cast<uint8_t>(kind)
        );
    }
    coalescing_ = coalesce;
    coalesced_kind_ = kind;
    coalesced_subject_ = subject;
    return true;
}

FLASHMEM bool ProjectSettingsHistoryService::undo(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation,
    MidiSyncState& midiSync
) {
    endCoalescing();
    if (undo_count_ == 0U || redo_count_ >= ENTRY_LIMIT) return false;
    const uint8_t slot = undo_slots_[undo_count_ - 1U];
    if (slot >= ENTRY_LIMIT || !entries_[slot].occupied) return false;
    auto& entry = entries_[slot];
    if (!sameProjectSettingsHistorySnapshot(
            captureProjectSettingsHistorySnapshot(statusBar, navigation, midiSync),
            entry.after
        ) ||
        !applyProjectSettingsHistorySnapshot(
            statusBar,
            navigation,
            midiSync,
            entry.before
        )) {
        return false;
    }
    --undo_count_;
    undo_slots_[undo_count_] = INVALID_SLOT;
    redo_slots_[redo_count_++] = slot;
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            ProjectHistoryDomain::Settings,
            identity_(slot),
            ProjectHistoryDirection::Undo
        );
    }
    return true;
}

FLASHMEM bool ProjectSettingsHistoryService::redo(
    StatusBarState& statusBar,
    ProjectNavigationState& navigation,
    MidiSyncState& midiSync
) {
    endCoalescing();
    if (redo_count_ == 0U || undo_count_ >= ENTRY_LIMIT) return false;
    const uint8_t slot = redo_slots_[redo_count_ - 1U];
    if (slot >= ENTRY_LIMIT || !entries_[slot].occupied) return false;
    auto& entry = entries_[slot];
    if (!sameProjectSettingsHistorySnapshot(
            captureProjectSettingsHistorySnapshot(statusBar, navigation, midiSync),
            entry.before
        ) ||
        !applyProjectSettingsHistorySnapshot(
            statusBar,
            navigation,
            midiSync,
            entry.after
        )) {
        return false;
    }
    --redo_count_;
    redo_slots_[redo_count_] = INVALID_SLOT;
    undo_slots_[undo_count_++] = slot;
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            ProjectHistoryDomain::Settings,
            identity_(slot),
            ProjectHistoryDirection::Redo
        );
    }
    return true;
}

FLASHMEM void ProjectSettingsHistoryService::clear() {
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyCleared(ProjectHistoryDomain::Settings);
    }
    entries_ = {};
    undo_slots_.fill(INVALID_SLOT);
    redo_slots_.fill(INVALID_SLOT);
    undo_count_ = 0U;
    redo_count_ = 0U;
    coalescing_ = false;
}

FLASHMEM void ProjectSettingsHistoryService::discardRedoBranch() {
    clearRedo_();
}

FLASHMEM uintptr_t ProjectSettingsHistoryService::projectHistoryUndoIdentity() const {
    return undo_count_ == 0U ? 0U : identity_(undo_slots_[undo_count_ - 1U]);
}

FLASHMEM uintptr_t ProjectSettingsHistoryService::projectHistoryRedoIdentity() const {
    return redo_count_ == 0U ? 0U : identity_(redo_slots_[redo_count_ - 1U]);
}

FLASHMEM uint8_t ProjectSettingsHistoryService::acquireSlot_() const {
    for (uint8_t slot = 0U; slot < ENTRY_LIMIT; ++slot) {
        if (!entries_[slot].occupied) return slot;
    }
    return INVALID_SLOT;
}

FLASHMEM void ProjectSettingsHistoryService::releaseSlot_(uint8_t slot) {
    if (slot < ENTRY_LIMIT) entries_[slot] = {};
}

FLASHMEM void ProjectSettingsHistoryService::evictOldestUndo_() {
    if (undo_count_ == 0U) return;
    const uint8_t slot = undo_slots_[0];
    if (project_history_sink_ != nullptr && slot < ENTRY_LIMIT) {
        project_history_sink_->notifyEvicted(
            ProjectHistoryDomain::Settings,
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

FLASHMEM void ProjectSettingsHistoryService::clearRedo_() {
    for (uint8_t index = 0U; index < redo_count_; ++index) {
        const uint8_t slot = redo_slots_[index];
        if (project_history_sink_ != nullptr && slot < ENTRY_LIMIT) {
            project_history_sink_->notifyEvicted(
                ProjectHistoryDomain::Settings,
                identity_(slot)
            );
        }
        releaseSlot_(slot);
        redo_slots_[index] = INVALID_SLOT;
    }
    redo_count_ = 0U;
}

FLASHMEM uintptr_t ProjectSettingsHistoryService::identity_(uint8_t slot) const {
    return slot < ENTRY_LIMIT
        ? reinterpret_cast<uintptr_t>(&entries_[slot])
        : 0U;
}

}  // namespace core::state::project
