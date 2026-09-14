#pragma once

#include <cstdint>

namespace core::state::project {

/** Read-only admission for Project Undo/Redo, independent of the input route. */
enum class ProjectHistoryBlockReason : uint8_t {
    NONE = 0,
    DRAFT,
    AUDITION,
    CAPTURE,
    GESTURE,
    SELECTION,
    PRESET_PREVIEW,
    LOCAL_EDITOR,
    PROJECT_CHANGE,
};

const char* projectHistoryBlockLabel(ProjectHistoryBlockReason reason);

}  // namespace core::state::project
