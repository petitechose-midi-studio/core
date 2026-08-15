#include "state/project/ProjectTrackEditorState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {

FLASHMEM void ProjectTrackEditorState::reset() {
    revision = 0U;
    trackIndex = 0U;
    selectedProperty = ProjectTrackEditorProperty::CHANNEL;
    currentKind = ProjectTrackEditorKind::INSTRUMENT;
    draftKind = ProjectTrackEditorKind::INSTRUMENT;
    active = false;
}

}  // namespace core::state::project
