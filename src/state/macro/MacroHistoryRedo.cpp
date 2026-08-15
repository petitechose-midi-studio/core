#include "state/macro/MacroHistory.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM bool MacroHistoryService::redo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress,
    MacroManualOverrideState* manualOverrides,
    core::state::project::ProjectTrackState* projectTracks
) {
    return replay_(
        core::state::project::ProjectHistoryDirection::Redo,
        pages,
        appliedAddress,
        manualOverrides,
        projectTracks
    );
}

}  // namespace core::state::macro
