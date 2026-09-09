#include "state/project/ProjectTrackEditorState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {

FLASHMEM void ProjectTrackEditorState::reset() {
    revision = 0U;
    trackIndex = 0U;
    selectedProperty = ProjectTrackEditorProperty::CHANNEL;
    currentKind = ProjectTrackEditorKind::INSTRUMENT;
    draftKind = ProjectTrackEditorKind::INSTRUMENT;
    nameDraft = {};
    textKeyIndex = core::state::interaction::TEXT_KEYBOARD_DEFAULT_INDEX;
    textOptRawPosition = 0.0f;
    textOptRowAccumulator = 0.0f;
    textEditing = false;
    textShiftActive = false;
    typeChangeBlocked = false;
    active = false;
}

}  // namespace core::state::project
