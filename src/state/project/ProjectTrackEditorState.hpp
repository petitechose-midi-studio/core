#pragma once

#include <cstdint>
#include <type_traits>

#include "state/interaction/TextKeyboardLayout.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::state::project {

/** Scalar properties exposed through the Track Editor contextual selector. */
enum class ProjectTrackEditorProperty : uint8_t {
    NAME = 0,
    CHANNEL,
    DELAY,
    TYPE,
    COUNT,
};

enum class ProjectTrackEditorKind : uint8_t {
    INSTRUMENT = 0,
    DRUM,
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
    ProjectTrackEditorKind currentKind = ProjectTrackEditorKind::INSTRUMENT;
    ProjectTrackEditorKind draftKind = ProjectTrackEditorKind::INSTRUMENT;
    ProjectTrackName nameDraft{};
    uint8_t textKeyIndex =
        core::state::interaction::TEXT_KEYBOARD_DEFAULT_INDEX;
    float textOptRawPosition = 0.0f;
    float textOptRowAccumulator = 0.0f;
    bool textEditing = false;
    bool textShiftActive = false;
    bool typeChangeBlocked = false;
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
               lhs.currentKind == rhs.currentKind &&
               lhs.draftKind == rhs.draftKind &&
               lhs.nameDraft == rhs.nameDraft &&
               lhs.textKeyIndex == rhs.textKeyIndex &&
               lhs.textOptRawPosition == rhs.textOptRawPosition &&
               lhs.textOptRowAccumulator == rhs.textOptRowAccumulator &&
               lhs.textEditing == rhs.textEditing &&
               lhs.textShiftActive == rhs.textShiftActive &&
               lhs.typeChangeBlocked == rhs.typeChangeBlocked &&
               lhs.active == rhs.active;
    }
};

static_assert(sizeof(ProjectTrackEditorState) <= 40U);
static_assert(std::is_trivially_copyable_v<ProjectTrackEditorState>);

}  // namespace core::state::project
