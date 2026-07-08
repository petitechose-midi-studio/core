#include "handler/sequencer/SequencerStructureStepOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerScaleState.hpp"

namespace core::handler {

FLASHMEM bool selectedStepRange(
    const oc::note::sequencer::StepBitMask128& mask,
    uint8_t activeLength,
    uint8_t& outFirst,
    uint8_t& outLast
) {
    bool found = false;
    outFirst = 0;
    outLast = 0;
    for (uint8_t step = 0; step < activeLength; ++step) {
        if (!mask.test(step)) continue;
        if (!found) {
            outFirst = step;
            found = true;
        }
        outLast = step;
    }
    return found;
}

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks
) {
    return core::state::sequencer::resolveEffectiveScaleSettings(
        tracks.projectScaleSettings(),
        sequencer.pattern.scalePolicy,
        sequencer.pattern.scaleOverride
    );
}

FLASHMEM bool resetActiveContentStep(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    StepResetDepth depth
) {
    if (step >= core::state::sequencer::activeContentLength(sequencer)) return false;

    bool changed = false;
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    if (core::state::sequencer::isRootContentView(sequencer)) {
        changed = sequencer.pattern.isEnabled(step) || changed;
        sequencer.pattern.setEnabled(step, false);
        changed = sequencer.setStepDataAt(
            step,
            core::state::sequencer::SequencerState::DEFAULT_NOTE,
            core::state::sequencer::SequencerState::DEFAULT_VELOCITY,
            core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT,
            0,
            core::state::sequencer::SequencerState::DEFAULT_PROBABILITY
        ) || changed;
        changed = (depth == StepResetDepth::Shallow
            ? core::state::sequencer::resetStepNodePayloadPreservingChildren(
                  sequencer.pattern,
                  nodeId
              )
            : core::state::sequencer::resetStepNodePayload(
                  sequencer.pattern,
                  nodeId
              )) || changed;
        return changed;
    }

    changed = (depth == StepResetDepth::Shallow
        ? core::state::sequencer::resetStepNodePayloadPreservingChildren(
              sequencer.pattern,
              nodeId,
              core::state::sequencer::SequencerGraphNodeResetMode::DISABLED_OVERRIDE
          )
        : core::state::sequencer::resetStepNodePayload(
              sequencer.pattern,
              nodeId,
              core::state::sequencer::SequencerGraphNodeResetMode::DISABLED_OVERRIDE
          )) || changed;
    if (changed) sequencer.contentView.bump();
    return changed;
}

FLASHMEM bool appendStepClipboardEntry(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    uint8_t firstStep,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    core::state::SequencerStepsClipboard& clipboard
) {
    if (clipboard.count >= clipboard.entries.size()) return false;

    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        step,
        scaleSettings
    );
    if (!projection.valid) return false;

    auto& entry = clipboard.entries[clipboard.count++];
    entry.valid = true;
    entry.offset = static_cast<uint8_t>(step - firstStep);
    entry.enabled = projection.enabled;
    entry.sourceNodeId = projection.nodeId;
    if (clipboard.rootContext) {
        entry.note = projection.parentNote;
        entry.velocity = projection.parentVelocity;
        entry.gate = projection.parentGate;
        entry.nudge = projection.parentNudge;
        entry.probability = projection.parentProbability;
    } else {
        entry.note = projection.note;
        entry.velocity = projection.velocity;
        entry.gate = projection.gate;
        entry.nudge = projection.nudge;
        entry.probability = projection.probability;
    }
    return true;
}

FLASHMEM bool writeRootStepFromClipboardEntry(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepClipboardEntry& entry,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetStep
) {
    if (targetStep >= core::state::sequencer::SequencerState::MAX_STEPS) return false;

    sequencer.pattern.setEnabled(targetStep, entry.enabled);
    (void)sequencer.setStepDataAt(
        targetStep,
        entry.note,
        entry.velocity,
        entry.gate,
        entry.nudge,
        entry.probability
    );

    const auto targetNode = core::state::sequencer::rootStepNodeId(targetStep);
    if (sourceGraph != nullptr &&
        entry.sourceNodeId != oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID) {
        return core::state::sequencer::copyStepNodePayloadFromGraph(
            sequencer.pattern,
            targetNode,
            *sourceGraph,
            entry.sourceNodeId
        );
    }

    (void)core::state::sequencer::resetStepNodePayload(sequencer.pattern, targetNode);
    return true;
}

FLASHMEM bool writeChildStepFromClipboardEntry(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepClipboardEntry& entry,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetStep
) {
    if (sourceGraph == nullptr ||
        entry.sourceNodeId == oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID ||
        targetStep >= core::state::sequencer::activeContentLength(sequencer)) {
        return false;
    }

    const auto targetNode = core::state::sequencer::activeContentStepNodeId(sequencer, targetStep);
    if (!core::state::sequencer::copyStepNodePayloadFromGraph(
            sequencer.pattern,
            targetNode,
            *sourceGraph,
            entry.sourceNodeId
        )) {
        return false;
    }
    sequencer.contentView.bump();
    return true;
}

}  // namespace core::handler
