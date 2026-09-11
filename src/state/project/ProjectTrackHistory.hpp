#pragma once

#include <cstddef>
#include <cstdint>

#include "state/project/ProjectHistorySlots.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::state::project {

enum class ProjectTrackHistoryActionKind : uint8_t {
    MidiChannel = 0,
    Delay,
    Mute,
    Solo,
    Name,
};

/**
 * One bounded, allocation-free Track control command.
 *
 * The payload stores complete snapshots so one gesture remains atomic
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

    [[nodiscard]] uint8_t undoCount() const { return slots_.undoCount(); }
    [[nodiscard]] uint8_t redoCount() const { return slots_.redoCount(); }
    [[nodiscard]] uintptr_t projectHistoryUndoIdentity() const;
    [[nodiscard]] uintptr_t projectHistoryRedoIdentity() const;
    [[nodiscard]] const ProjectTrackHistoryEntry* peekUndo() const;
    [[nodiscard]] const ProjectTrackHistoryEntry* peekRedo() const;
    [[nodiscard]] constexpr size_t retainedBytes() const {
        return slots_.retainedBytes();
    }

private:
    [[nodiscard]] bool record_(
        const ProjectTrackSnapshot& before,
        const ProjectTrackSnapshot& after,
        ProjectTrackHistoryActionKind kind,
        uint8_t trackIndex
    );
    [[nodiscard]] bool apply_(ProjectTrackState& state, ProjectHistoryDirection direction);

    struct PendingGesture {
        ProjectTrackSnapshot before{};
        ProjectTrackHistoryActionKind kind =
            ProjectTrackHistoryActionKind::MidiChannel;
        uint8_t trackIndex = PROJECT_TRACK_COUNT;
        bool active = false;
    };

    ProjectHistorySlots<ProjectTrackHistoryEntry, ProjectHistoryDomain::Track, ENTRY_LIMIT> slots_{};
    PendingGesture pending_gesture_{};
    const ProjectHistoryEventSink* project_history_sink_ = nullptr;
};

}  // namespace core::state::project
