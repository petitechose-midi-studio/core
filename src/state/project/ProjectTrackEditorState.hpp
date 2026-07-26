#pragma once

#include <cstdint>
#include <type_traits>

namespace core::state::project {

/** Scalar properties exposed through the Track Editor contextual selector. */
enum class ProjectTrackEditorProperty : uint8_t {
    CHANNEL = 0,
    DELAY,
    COUNT,
};

/**
 * Compact session-only state for the retained Track Editor.
 *
 * Authored Track values deliberately do not live here. Retargeting only
 * changes the Track identity; the view model then reads one coherent set of
 * values from ProjectTrackState. This avoids cached routing/mix mirrors while
 * switching Tracks.
 */
struct ProjectTrackEditorState {
    uint32_t revision = 0U;
    uint8_t trackIndex = 0U;
    ProjectTrackEditorProperty selectedProperty =
        ProjectTrackEditorProperty::CHANNEL;
    bool active = false;

    /**
     * Compact visibility-binding contract for ExclusiveVisibilityStack.
     *
     * Keeping the binding on this session state avoids a duplicate visibility
     * signal (and its subscriber storage) while still letting overlay
     * presentation attach/park the retained UI tree canonically.
     */
    [[nodiscard]] bool get() const { return active; }
    void set(bool nextActive) {
        if (active == nextActive) return;
        active = nextActive;
        ++revision;
        if (revision == 0U) revision = 1U;
    }

    /** Lifecycle reset; interactive changes go through EditorOps. */
    void reset();

    friend constexpr bool operator==(
        const ProjectTrackEditorState& lhs,
        const ProjectTrackEditorState& rhs
    ) {
        return lhs.revision == rhs.revision &&
               lhs.trackIndex == rhs.trackIndex &&
               lhs.selectedProperty == rhs.selectedProperty &&
               lhs.active == rhs.active;
    }
};

static_assert(sizeof(ProjectTrackEditorState) == 8U);
static_assert(std::is_trivially_copyable_v<ProjectTrackEditorState>);

}  // namespace core::state::project
