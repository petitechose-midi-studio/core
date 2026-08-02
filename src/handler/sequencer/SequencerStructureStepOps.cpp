#include "handler/sequencer/SequencerStructureStepOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerScaleState.hpp"

namespace core::handler {

namespace {

FLASHMEM bool selectedStepRange(
    const oc::note::sequencer::StepBitMask128& mask,
    uint8_t activeLength,
    uint8_t& outFirst,
    uint8_t& outLast
) {
    bool found = false;
    outFirst = 0;
    outLast = 0;
    for (uint16_t step = 0; step < activeLength; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        if (!mask.test(stepIndex)) continue;
        if (!found) {
            outFirst = stepIndex;
            found = true;
        }
        outLast = stepIndex;
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

}  // namespace

FLASHMEM bool captureFocusedStepClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    uint8_t step,
    core::state::SequencerStepsClipboard& clipboard
) {
    if (step >= core::state::sequencer::activeContentLength(sequencer)) return false;

    clipboard = {};
    clipboard.valid = true;
    clipboard.rootContext = core::state::sequencer::isRootContentView(sequencer);
    clipboard.span = 1;

    return appendStepClipboardEntry(
        sequencer,
        step,
        step,
        effectiveScaleSettings(sequencer, tracks),
        clipboard
    );
}

FLASHMEM bool captureStepSelectionClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const oc::note::sequencer::StepBitMask128& selectedMask,
    core::state::SequencerStepsClipboard& clipboard
) {
    const uint8_t activeLength = core::state::sequencer::activeContentLength(sequencer);
    uint8_t first = 0;
    uint8_t last = 0;
    if (!selectedStepRange(selectedMask, activeLength, first, last)) return false;

    clipboard = {};
    clipboard.valid = true;
    clipboard.rootContext = core::state::sequencer::isRootContentView(sequencer);
    clipboard.span = static_cast<uint8_t>(last - first + 1U);

    const auto scaleSettings = effectiveScaleSettings(sequencer, tracks);
    for (uint8_t step = first; step <= last; ++step) {
        if (!selectedMask.test(step)) continue;
        if (clipboard.count >= clipboard.entries.size()) break;

        (void)appendStepClipboardEntry(sequencer, step, first, scaleSettings, clipboard);
    }

    return clipboard.count > 0;
}

}  // namespace core::handler
