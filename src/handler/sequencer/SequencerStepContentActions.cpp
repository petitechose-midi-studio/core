#include "SequencerStepHandler.hpp"

#include <config/PlatformCompat.hpp>

#include <utility>

#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {

FLASHMEM bool SequencerStepHandler::focusedStepHasChildContent() const {
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer_,
        sequencer_.focusedStep.get()
    );
    return core::state::sequencer::stepNodeHasAnyChildContent(
        sequencer_.pattern,
        nodeId
    );
}

FLASHMEM bool SequencerStepHandler::canPasteFocusedStepContent() const {
    return structure_clipboard_.hasSequencerStepContent(
               core::state::SequencerStepContentClipboardKind::ALL
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               sequencer_,
               sequencer_.focusedStep.get()
           );
}

FLASHMEM void SequencerStepHandler::recordFocusedContentEdit(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    bool beforeCaptured
) {
    if (!beforeCaptured) return;

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    if (!core::state::sequencer::captureHistorySnapshot(sequencer_, after)) return;
    if (core::state::sequencer::sameMusicalHistorySnapshot(before, after)) return;

    history_.recordPattern(
        std::move(before),
        std::move(after),
        core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = sequencer_.focusedStep.get(),
            .property = core::state::sequencer::StepProperty::NOTE,
            .hasValue = false,
        }
    );
}

FLASHMEM void SequencerStepHandler::clearFocusedStepContent() {
    if (!focusedStepHasChildContent()) return;
    history_.commitCoalescedPatternEdit();

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    const bool beforeCaptured =
        core::state::sequencer::captureHistorySnapshot(sequencer_, before);

    if (!core::state::sequencer::clearActiveContentChildren(
            sequencer_,
            sequencer_.focusedStep.get()
        )) {
        return;
    }
    recordFocusedContentEdit(std::move(before), beforeCaptured);
}

FLASHMEM void SequencerStepHandler::copyFocusedStepContent() {
    if (!focusedStepHasChildContent()) return;
    (void)core::state::sequencer::copyActiveContentChildrenToClipboard(
        sequencer_,
        sequencer_.focusedStep.get(),
        structure_clipboard_
    );
}

FLASHMEM void SequencerStepHandler::pasteFocusedStepContent() {
    if (!canPasteFocusedStepContent()) return;
    history_.commitCoalescedPatternEdit();

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    const bool beforeCaptured =
        core::state::sequencer::captureHistorySnapshot(sequencer_, before);

    if (!core::state::sequencer::pasteActiveContentChildrenFromClipboard(
            sequencer_,
            sequencer_.focusedStep.get(),
            structure_clipboard_
        )) {
        return;
    }
    recordFocusedContentEdit(std::move(before), beforeCaptured);
}

}  // namespace core::handler
