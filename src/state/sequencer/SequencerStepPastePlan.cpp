#include "state/sequencer/SequencerStepPastePlan.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM void appendPreviewEntry(
    SequencerStepPastePreviewPlan& plan,
    uint8_t clipboardIndex,
    uint8_t target,
    SequencerStepPastePreview preview
) {
    if (plan.count >= plan.entries.size()) return;

    if (plan.count == 0) {
        plan.firstTarget = target;
        plan.lastTarget = target;
    } else {
        plan.firstTarget = std::min(plan.firstTarget, target);
        plan.lastTarget = std::max(plan.lastTarget, target);
    }

    auto& entry = plan.entries[plan.count++];
    entry.valid = true;
    entry.clipboardIndex = clipboardIndex;
    entry.targetStep = target;
    entry.preview = preview;
}

FLASHMEM SequencerStepPastePreview aggregatePreview(
    bool blocked,
    bool hasGhost,
    bool hasOverwrite,
    bool hasEntry
) {
    if (blocked) return SequencerStepPastePreview::BLOCKED;
    if (hasGhost) return SequencerStepPastePreview::GHOST;
    if (hasOverwrite) return SequencerStepPastePreview::OVERWRITE;
    return hasEntry ? SequencerStepPastePreview::EMPTY
                    : SequencerStepPastePreview::NONE;
}

}  // namespace

FLASHMEM uint8_t maxStepCursorForPaste(const SequencerState& sequencer) {
    if (isMicroSequenceContentView(sequencer)) {
        return static_cast<uint8_t>(
            oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP - 1U
        );
    }
    if (isCycleStatesContentView(sequencer)) {
        return static_cast<uint8_t>(
            oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET - 1U
        );
    }
    return static_cast<uint8_t>(SequencerState::MAX_STEPS - 1U);
}

FLASHMEM uint8_t requiredStepPasteLength(
    core::state::project::ProjectStepPasteMode mode,
    uint8_t lastTarget
) {
    if (mode == core::state::project::ProjectStepPasteMode::PAGE) {
        return static_cast<uint8_t>(
            ((lastTarget / SequencerState::STEPS_PER_PAGE) + 1U) *
            SequencerState::STEPS_PER_PAGE
        );
    }
    return static_cast<uint8_t>(lastTarget + 1U);
}

FLASHMEM bool resolveStepPasteTarget(
    core::state::project::ProjectStepPasteMode mode,
    uint8_t cursor,
    uint8_t offset,
    uint8_t activeLength,
    uint8_t maxStep,
    uint8_t& outStep
) {
    if (mode == core::state::project::ProjectStepPasteMode::WRAP) {
        if (activeLength == 0) return false;
        outStep = static_cast<uint8_t>((static_cast<uint16_t>(cursor) + offset) % activeLength);
        return true;
    }

    const uint16_t target = static_cast<uint16_t>(cursor) + offset;
    if (target > maxStep) return false;
    outStep = static_cast<uint8_t>(target);
    return true;
}

FLASHMEM SequencerStepPastePreviewPlan buildStepPastePreviewPlan(
    const core::state::SequencerStepsClipboard& clipboard,
    bool targetRootContext,
    uint8_t cursor,
    uint8_t activeLength,
    uint8_t maxStep,
    core::state::project::ProjectStepPasteMode mode
) {
    SequencerStepPastePreviewPlan plan;
    if (!clipboard.valid || clipboard.count == 0) {
        return plan;
    }

    if (clipboard.rootContext != targetRootContext) {
        plan.blocked = true;
        plan.aggregate = SequencerStepPastePreview::BLOCKED;
        return plan;
    }

    const auto pasteMode = core::state::project::sanitizeProjectStepPasteMode(mode);
    bool hasGhost = false;
    bool hasOverwrite = false;
    bool hasValidClipboardEntry = false;

    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const auto& entry = clipboard.entries[i];
        if (!entry.valid) continue;
        hasValidClipboardEntry = true;

        uint8_t target = 0;
        if (!resolveStepPasteTarget(
                pasteMode,
                cursor,
                entry.offset,
                activeLength,
                maxStep,
                target
            )) {
            plan.blocked = true;
            continue;
        }

        const auto preview = target >= activeLength
            ? SequencerStepPastePreview::GHOST
            : SequencerStepPastePreview::OVERWRITE;
        hasGhost = hasGhost || preview == SequencerStepPastePreview::GHOST;
        hasOverwrite = hasOverwrite || preview == SequencerStepPastePreview::OVERWRITE;
        appendPreviewEntry(plan, i, target, preview);
    }

    plan.aggregate =
        aggregatePreview(plan.blocked, hasGhost, hasOverwrite, hasValidClipboardEntry);
    return plan;
}

FLASHMEM bool resizeActiveContentForStepPaste(
    SequencerState& sequencer,
    core::state::project::ProjectStepPasteMode mode,
    uint8_t lastTarget,
    uint8_t maxStep
) {
    if (mode == core::state::project::ProjectStepPasteMode::WRAP) return true;

    const uint8_t required = requiredStepPasteLength(mode, lastTarget);
    if (required == 0 || required > static_cast<uint8_t>(maxStep + 1U)) return false;

    const uint8_t current = activeContentLength(sequencer);
    if (current >= required) return true;

    if (isRootContentView(sequencer)) {
        sequencer.pattern.length.set(required);
        return true;
    }
    if (isMicroSequenceContentView(sequencer)) {
        return resizeActiveMicroSequenceContent(sequencer, required);
    }
    if (isCycleStatesContentView(sequencer)) {
        return resizeActiveCycleStatesContent(sequencer, required);
    }
    return false;
}

}  // namespace core::state::sequencer
