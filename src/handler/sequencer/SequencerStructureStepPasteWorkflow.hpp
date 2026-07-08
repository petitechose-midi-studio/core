#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"

namespace core::handler {

core::state::project::ProjectStepPasteMode structureStepPasteMode(
    const core::state::project::ProjectNavigationState& projectNavigation
);

core::state::sequencer::SequencerStepPastePreviewPlan buildStructureStepPastePlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepsClipboard& clipboard,
    core::state::project::ProjectStepPasteMode mode,
    uint8_t cursorStep
);

void beginStructureStepPastePreview(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    const core::state::project::ProjectNavigationState& projectNavigation
);

void clearStructureStepPastePreview(
    core::state::sequencer::SequencerState& sequencer
);

bool commitStructureStepPastePlan(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    core::state::project::ProjectStepPasteMode mode,
    const core::state::sequencer::SequencerStepPastePreviewPlan& plan
);

}  // namespace core::handler
