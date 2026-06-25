#pragma once

#include <array>
#include <cstdint>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state {
struct SequencerStepsClipboard;
}

namespace core::state::sequencer {

struct SequencerStepPastePreviewEntry {
    bool valid = false;
    uint8_t clipboardIndex = 0;
    uint8_t targetStep = 0;
    SequencerStepPastePreview preview = SequencerStepPastePreview::NONE;
};

/**
 * Shared step-paste destination plan used by both UI preview and paste commit.
 */
struct SequencerStepPastePreviewPlan {
    bool blocked = false;
    uint8_t count = 0;
    uint8_t firstTarget = 0;
    uint8_t lastTarget = 0;
    SequencerStepPastePreview aggregate = SequencerStepPastePreview::NONE;
    std::array<SequencerStepPastePreviewEntry, SequencerState::MAX_STEPS> entries{};

    bool hasEntries() const { return count > 0; }
};

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

SequencerStepPastePreviewPlan buildStepPastePreviewPlan(
    const core::state::SequencerStepsClipboard& clipboard,
    bool targetRootContext,
    uint8_t cursor,
    uint8_t activeLength,
    uint8_t maxStep,
    core::state::project::ProjectStepPasteMode mode
);

bool resizeActiveContentForStepPaste(
    SequencerState& sequencer,
    core::state::project::ProjectStepPasteMode mode,
    uint8_t lastTarget,
    uint8_t maxStep
);

}  // namespace core::state::sequencer
