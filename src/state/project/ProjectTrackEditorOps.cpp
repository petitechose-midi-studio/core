#include "state/project/ProjectTrackEditorOps.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {
namespace {

FLASHMEM ProjectTrackEditorMutationResult result(
    ProjectTrackEditorMutationStatus status,
    uint8_t track = PROJECT_TRACK_EDITOR_NO_TRACK
) {
    return {
        .status = status,
        .trackIndex = track,
    };
}

FLASHMEM void bumpRevision(ProjectTrackEditorState& editor) {
    ++editor.revision;
    if (editor.revision == 0U) editor.revision = 1U;
}

}  // namespace

FLASHMEM uint8_t nextEnabledProjectTrack(
    uint16_t enabledMask,
    uint8_t currentTrack,
    int direction
) {
    if (enabledMask == 0U) return PROJECT_TRACK_EDITOR_NO_TRACK;
    if (direction == 0) {
        return projectTrackEditorTrackEnabled(enabledMask, currentTrack)
            ? currentTrack
            : PROJECT_TRACK_EDITOR_NO_TRACK;
    }

    const int step = direction > 0 ? 1 : -1;
    const int start = currentTrack < PROJECT_TRACK_COUNT
        ? static_cast<int>(currentTrack)
        : (step > 0 ? static_cast<int>(PROJECT_TRACK_COUNT) - 1 : 0);
    for (uint8_t offset = 1U; offset <= PROJECT_TRACK_COUNT; ++offset) {
        int candidate = start + step * static_cast<int>(offset);
        candidate %= static_cast<int>(PROJECT_TRACK_COUNT);
        if (candidate < 0) candidate += PROJECT_TRACK_COUNT;
        const auto track = static_cast<uint8_t>(candidate);
        if (projectTrackEditorTrackEnabled(enabledMask, track)) return track;
    }
    return PROJECT_TRACK_EDITOR_NO_TRACK;
}

FLASHMEM ProjectTrackEditorMutationResult openProjectTrackEditor(
    ProjectTrackEditorState& editor,
    uint8_t track,
    uint16_t enabledMask
) {
    if (track >= PROJECT_TRACK_COUNT) {
        return result(ProjectTrackEditorMutationStatus::INVALID_TRACK, track);
    }
    if (!projectTrackEditorTrackEnabled(enabledMask, track)) {
        return result(ProjectTrackEditorMutationStatus::TRACK_DISABLED, track);
    }
    if (editor.active && editor.trackIndex == track) {
        return result(ProjectTrackEditorMutationStatus::NO_CHANGE, track);
    }

    editor.trackIndex = track;
    editor.active = true;
    bumpRevision(editor);
    return result(ProjectTrackEditorMutationStatus::OK, track);
}

FLASHMEM ProjectTrackEditorMutationResult closeProjectTrackEditor(
    ProjectTrackEditorState& editor
) {
    if (!editor.active) {
        return result(
            ProjectTrackEditorMutationStatus::NO_CHANGE,
            editor.trackIndex
        );
    }
    editor.active = false;
    bumpRevision(editor);
    return result(ProjectTrackEditorMutationStatus::OK, editor.trackIndex);
}

FLASHMEM ProjectTrackEditorMutationResult retargetProjectTrackEditor(
    ProjectTrackEditorState& editor,
    uint8_t track,
    uint16_t enabledMask
) {
    if (!editor.active) {
        return result(ProjectTrackEditorMutationStatus::INACTIVE, track);
    }
    if (track >= PROJECT_TRACK_COUNT) {
        return result(ProjectTrackEditorMutationStatus::INVALID_TRACK, track);
    }
    if (!projectTrackEditorTrackEnabled(enabledMask, track)) {
        return result(ProjectTrackEditorMutationStatus::TRACK_DISABLED, track);
    }
    if (editor.trackIndex == track) {
        return result(ProjectTrackEditorMutationStatus::NO_CHANGE, track);
    }

    editor.trackIndex = track;
    bumpRevision(editor);
    return result(ProjectTrackEditorMutationStatus::OK, track);
}

FLASHMEM ProjectTrackEditorMutationResult moveProjectTrackEditorTrack(
    ProjectTrackEditorState& editor,
    uint16_t enabledMask,
    int direction
) {
    if (!editor.active) {
        return result(
            ProjectTrackEditorMutationStatus::INACTIVE,
            editor.trackIndex
        );
    }
    if (direction == 0) {
        return result(
            ProjectTrackEditorMutationStatus::NO_CHANGE,
            editor.trackIndex
        );
    }
    const uint8_t target = nextEnabledProjectTrack(
        enabledMask,
        editor.trackIndex,
        direction
    );
    if (target == PROJECT_TRACK_EDITOR_NO_TRACK) {
        return result(ProjectTrackEditorMutationStatus::NO_ENABLED_TRACK);
    }
    return retargetProjectTrackEditor(editor, target, enabledMask);
}

FLASHMEM ProjectTrackEditorMutationResult selectProjectTrackEditorProperty(
    ProjectTrackEditorState& editor,
    ProjectTrackEditorProperty property
) {
    if (!editor.active) {
        return result(
            ProjectTrackEditorMutationStatus::INACTIVE,
            editor.trackIndex
        );
    }
    if (!validProjectTrackEditorProperty(property)) {
        return result(
            ProjectTrackEditorMutationStatus::INVALID_PROPERTY,
            editor.trackIndex
        );
    }
    if (editor.selectedProperty == property) {
        return result(
            ProjectTrackEditorMutationStatus::NO_CHANGE,
            editor.trackIndex
        );
    }

    editor.selectedProperty = property;
    bumpRevision(editor);
    return result(ProjectTrackEditorMutationStatus::OK, editor.trackIndex);
}

FLASHMEM ProjectTrackEditorMutationResult moveProjectTrackEditorProperty(
    ProjectTrackEditorState& editor,
    int direction
) {
    if (!editor.active) {
        return result(
            ProjectTrackEditorMutationStatus::INACTIVE,
            editor.trackIndex
        );
    }
    if (direction == 0) {
        return result(
            ProjectTrackEditorMutationStatus::NO_CHANGE,
            editor.trackIndex
        );
    }
    if (!validProjectTrackEditorProperty(editor.selectedProperty)) {
        return result(
            ProjectTrackEditorMutationStatus::INVALID_PROPERTY,
            editor.trackIndex
        );
    }

    constexpr uint8_t count =
        static_cast<uint8_t>(ProjectTrackEditorProperty::COUNT);
    const int step = direction > 0 ? 1 : -1;
    int next = static_cast<int>(editor.selectedProperty) + step;
    if (next < 0) next += count;
    if (next >= count) next -= count;
    return selectProjectTrackEditorProperty(
        editor,
        static_cast<ProjectTrackEditorProperty>(next)
    );
}

}  // namespace core::state::project
