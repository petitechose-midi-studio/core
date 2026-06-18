#pragma once

#include <cstdint>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

uint8_t maxStepCursorForPaste(const SequencerState& sequencer);

uint8_t requiredStepPasteLength(
    core::state::project::ProjectStepPasteMode mode,
    uint8_t lastTarget
);

bool resolveStepPasteTarget(
    core::state::project::ProjectStepPasteMode mode,
    uint8_t cursor,
    uint8_t offset,
    uint8_t activeLength,
    uint8_t maxStep,
    uint8_t& outStep
);

bool resizeActiveContentForStepPaste(
    SequencerState& sequencer,
    core::state::project::ProjectStepPasteMode mode,
    uint8_t lastTarget,
    uint8_t maxStep
);

}  // namespace core::state::sequencer
