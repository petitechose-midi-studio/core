#include "SequencerStepHandler.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {

namespace {

namespace seq = core::state::sequencer;

constexpr auto kStepContentOwner = seq::SequencerPreparedPatternEditOwner::StepContent;
constexpr uint8_t kPasteKeyFlag = 0x80U;
static_assert(seq::SequencerPatternState::MAX_STEPS <= kPasteKeyFlag,
              "Step Content action key must encode every Pattern step");

FLASHMEM seq::SequencerHistoryDescriptor stepContentDescriptor(uint8_t step) {
    return {
        .kind = seq::SequencerHistoryActionKind::StepEdit,
        .stepIndex = step,
        .property = seq::StepProperty::NOTE,
        .hasValue = false,
    };
}

FLASHMEM bool preparedBeginAccepted(seq::SequencerState& sequencer,
                                    seq::SequencerPreparedPatternEditBeginOutcome outcome) {
    if (seq::sequencerHistoryOpenAccepted(outcome)) return true;
    sequencer.historyFeedback.showRejection(outcome, oc::time::millis());
    return false;
}

FLASHMEM void showHistoryUnavailable(seq::SequencerState& sequencer) {
    sequencer.historyFeedback.showRejection(
        seq::SequencerHistoryRejectionReason::HistoryUnavailable, oc::time::millis());
}

FLASHMEM seq::SequencerCoalescedPatternPayloadPlan pastePayloadPlan(
    const seq::SequencerState& sequencer) {
    return seq::graphView(sequencer.pattern) != nullptr
               ? seq::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload
               : seq::SequencerCoalescedPatternPayloadPlan::FullWithProspectiveGraph;
}

}  // namespace

FLASHMEM bool SequencerStepHandler::focusedStepHasChildContent() const {
    const auto nodeId =
        core::state::sequencer::activeContentStepNodeId(sequencer_, sequencer_.focusedStep.get());
    return core::state::sequencer::stepNodeHasAnyChildContent(sequencer_.pattern, nodeId);
}

FLASHMEM bool SequencerStepHandler::canPasteFocusedStepContent() const {
    return structure_clipboard_.hasSequencerStepContent(
               core::state::SequencerStepContentClipboardKind::ALL) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               sequencer_, sequencer_.focusedStep.get());
}

FLASHMEM void SequencerStepHandler::clearFocusedStepContent() {
    if (!focusedStepHasChildContent()) return;
    const uint8_t step = sequencer_.focusedStep.get();

    // A detached Micro/Cycle draft owns its own final prepared publication.
    // Editing that scratch must neither allocate nor create an intermediate
    // Pattern History entry.
    if (sequencer_.stepContentDraft.pattern() != nullptr) {
        (void)seq::clearActiveContentChildren(sequencer_, step);
        return;
    }

    const auto descriptor = stepContentDescriptor(step);
    if (!preparedBeginAccepted(
            sequencer_,
            history_.beginPreparedPatternEdit(
            kStepContentOwner, step, seq::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload,
            descriptor, true))) {
        return;
    }

    const bool changed = seq::clearActiveContentChildrenPreservingGraphOwner(sequencer_, step);
    const auto seal =
        history_.sealPreparedPatternEdit(kStepContentOwner, step, changed, descriptor);
    if (seq::sequencerPreparedPatternEditSealFailed(seal)) {
        showHistoryUnavailable(sequencer_);
        return;
    }
    if (seal != seq::SequencerPreparedPatternEditSealOutcome::Sealed) return;

    const auto commit = history_.commitPreparedPatternEdit(kStepContentOwner);
    if (commit != seq::SequencerPreparedPatternEditCommitOutcome::Committed) {
        showHistoryUnavailable(sequencer_);
        return; }
}

FLASHMEM void SequencerStepHandler::copyFocusedStepContent() {
    if (!focusedStepHasChildContent()) return;
    (void)core::state::sequencer::copyActiveContentChildrenToClipboard(
        sequencer_, sequencer_.focusedStep.get(), structure_clipboard_);
}

FLASHMEM void SequencerStepHandler::pasteFocusedStepContent() {
    if (!canPasteFocusedStepContent()) return;
    const uint8_t step = sequencer_.focusedStep.get();

    if (sequencer_.stepContentDraft.pattern() != nullptr) {
        (void)seq::pasteActiveContentChildrenFromClipboard(sequencer_, step, structure_clipboard_);
        return;
    }

    const uint8_t key = static_cast<uint8_t>(step | kPasteKeyFlag);
    const auto descriptor = stepContentDescriptor(step);
    if (!preparedBeginAccepted(sequencer_, history_.beginPreparedPatternEdit(
            kStepContentOwner, key, pastePayloadPlan(sequencer_), descriptor, true))) {
        return;
    }

    const bool changed = seq::pasteActiveContentChildrenFromClipboardPreservingGraphOwner(
        sequencer_, step, structure_clipboard_);
    const auto seal = history_.sealPreparedPatternEdit(kStepContentOwner, key, changed, descriptor);
    if (seq::sequencerPreparedPatternEditSealFailed(seal)) {
        showHistoryUnavailable(sequencer_);
        return;
    }
    if (seal != seq::SequencerPreparedPatternEditSealOutcome::Sealed) return;

    const auto commit = history_.commitPreparedPatternEdit(kStepContentOwner);
    if (commit != seq::SequencerPreparedPatternEditCommitOutcome::Committed) {
        showHistoryUnavailable(sequencer_);
        return; }
}

}  // namespace core::handler
