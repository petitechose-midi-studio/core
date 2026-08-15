#pragma once

#include <cstdint>

#include "state/project/ProjectTrackEditorState.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::state::project {

inline constexpr uint8_t PROJECT_TRACK_EDITOR_NO_TRACK = PROJECT_TRACK_COUNT;

enum class ProjectTrackEditorMutationStatus : uint8_t {
    OK = 0,
    NO_CHANGE,
    INACTIVE,
    INVALID_TRACK,
    TRACK_DISABLED,
    NO_ENABLED_TRACK,
    INVALID_PROPERTY,
    DIRTY_DRAFT,
};

struct ProjectTrackEditorMutationResult {
    ProjectTrackEditorMutationStatus status =
        ProjectTrackEditorMutationStatus::NO_CHANGE;
    uint8_t trackIndex = PROJECT_TRACK_EDITOR_NO_TRACK;

    [[nodiscard]] constexpr bool changed() const {
        return status == ProjectTrackEditorMutationStatus::OK;
    }
};

[[nodiscard]] constexpr bool validProjectTrackEditorProperty(
    ProjectTrackEditorProperty property
) {
    return static_cast<uint8_t>(property) <
        static_cast<uint8_t>(ProjectTrackEditorProperty::COUNT);
}

[[nodiscard]] constexpr bool projectTrackEditorTrackEnabled(
    uint16_t enabledMask,
    uint8_t track
) {
    return track < PROJECT_TRACK_COUNT &&
        (enabledMask & static_cast<uint16_t>(1U << track)) != 0U;
}

[[nodiscard]] constexpr bool projectTrackEditorKindDraftDirty(
    const ProjectTrackEditorState& editor
) {
    return editor.active && editor.currentKind != editor.draftKind;
}

/**
 * Allocation-free wrapped lookup over enabled Tracks only.
 *
 * Positive and negative directions select the next/previous enabled Track.
 * A disabled current Track is skipped. An invalid current Track starts at the
 * first/last enabled Track respectively. Zero never changes the Track.
 */
[[nodiscard]] uint8_t nextEnabledProjectTrack(
    uint16_t enabledMask,
    uint8_t currentTrack,
    int direction
);

ProjectTrackEditorMutationResult openProjectTrackEditor(
    ProjectTrackEditorState& editor,
    uint8_t track,
    uint16_t enabledMask
);

ProjectTrackEditorMutationResult closeProjectTrackEditor(
    ProjectTrackEditorState& editor
);

/**
 * Hot-retargets the retained editor without caching authored values.
 *
 * A pending type conversion is a visible draft and therefore keeps ownership
 * of its opening Track until Apply or Cancel.
 */
ProjectTrackEditorMutationResult retargetProjectTrackEditor(
    ProjectTrackEditorState& editor,
    uint8_t track,
    uint16_t enabledMask
);

ProjectTrackEditorMutationResult moveProjectTrackEditorTrack(
    ProjectTrackEditorState& editor,
    uint16_t enabledMask,
    int direction
);

ProjectTrackEditorMutationResult selectProjectTrackEditorProperty(
    ProjectTrackEditorState& editor,
    ProjectTrackEditorProperty property
);

ProjectTrackEditorMutationResult moveProjectTrackEditorProperty(
    ProjectTrackEditorState& editor,
    int direction
);

ProjectTrackEditorMutationResult syncProjectTrackEditorKind(
    ProjectTrackEditorState& editor,
    ProjectTrackEditorKind kind
);

ProjectTrackEditorMutationResult selectProjectTrackEditorDraftKind(
    ProjectTrackEditorState& editor,
    ProjectTrackEditorKind kind
);

}  // namespace core::state::project
