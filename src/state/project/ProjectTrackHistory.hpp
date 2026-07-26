#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "state/project/ProjectHistoryEventSink.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::state::project {

enum class ProjectTrackHistoryActionKind : uint8_t {
    MidiChannel = 0,
    Delay,
    Mute,
    Solo,
};

/**
 * One bounded, allocation-free Track control command.
 *
 * The payload stores complete 52-byte snapshots so one gesture remains atomic
 * even when a control changes the global audible mask (Solo/Mute). Entries are
 * held in stable slots: their addresses can therefore be used safely by the
 * Project-wide chronology while commands move between Undo and Redo.
 */
struct ProjectTrackHistoryEntry {
    ProjectTrackSnapshot before{};
    ProjectTrackSnapshot after{};
    ProjectTrackHistoryActionKind kind =
        ProjectTrackHistoryActionKind::MidiChannel;
    uint8_t trackIndex = PROJECT_TRACK_COUNT;
    bool occupied = false;
};

class ProjectTrackHistoryService {
public:
    static constexpr uint8_t ENTRY_LIMIT = 8U;

    void setProjectHistoryEventSink(const ProjectHistoryEventSink* sink) {
        project_history_sink_ = sink;
    }

    [[nodiscard]] bool beginGesture(
        const ProjectTrackState& state,
        ProjectTrackHistoryActionKind kind,
        uint8_t trackIndex
    );
    [[nodiscard]] bool gestureMatches(
        ProjectTrackHistoryActionKind kind,
        uint8_t trackIndex
    ) const;
    [[nodiscard]] bool commitGesture(ProjectTrackState& state);
    [[nodiscard]] bool cancelGesture(ProjectTrackState& state);
    [[nodiscard]] bool hasPendingGesture() const {
        return pending_gesture_.active;
    }

    [[nodiscard]] bool undo(ProjectTrackState& state);
    [[nodiscard]] bool redo(ProjectTrackState& state);
    void clear();
    void discardRedoBranch();

    [[nodiscard]] uint8_t undoCount() const { return undo_count_; }
    [[nodiscard]] uint8_t redoCount() const { return redo_count_; }
    [[nodiscard]] uintptr_t projectHistoryUndoIdentity() const;
    [[nodiscard]] uintptr_t projectHistoryRedoIdentity() const;
    [[nodiscard]] const ProjectTrackHistoryEntry* peekUndo() const;
    [[nodiscard]] const ProjectTrackHistoryEntry* peekRedo() const;
    [[nodiscard]] constexpr size_t retainedBytes() const {
        return sizeof(entries_) + sizeof(undo_slots_) + sizeof(redo_slots_);
    }

private:
    static constexpr uint8_t INVALID_SLOT = ENTRY_LIMIT;

    [[nodiscard]] uint8_t acquireSlot_();
    [[nodiscard]] bool record_(
        const ProjectTrackSnapshot& before,
        const ProjectTrackSnapshot& after,
        ProjectTrackHistoryActionKind kind,
        uint8_t trackIndex
    );
    void releaseSlot_(uint8_t slot);
    void evictOldestUndo_();
    void clearRedo_();
    [[nodiscard]] uintptr_t identity_(uint8_t slot) const;

    struct PendingGesture {
        ProjectTrackSnapshot before{};
        ProjectTrackHistoryActionKind kind =
            ProjectTrackHistoryActionKind::MidiChannel;
        uint8_t trackIndex = PROJECT_TRACK_COUNT;
        bool active = false;
    };

    std::array<ProjectTrackHistoryEntry, ENTRY_LIMIT> entries_{};
    std::array<uint8_t, ENTRY_LIMIT> undo_slots_{};
    std::array<uint8_t, ENTRY_LIMIT> redo_slots_{};
    uint8_t undo_count_ = 0U;
    uint8_t redo_count_ = 0U;
    PendingGesture pending_gesture_{};
    const ProjectHistoryEventSink* project_history_sink_ = nullptr;
};

}  // namespace core::state::project
