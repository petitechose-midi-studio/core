#include "handler/sequencer/SequencerStepEditSessionWorkflow.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler::sequencer::step_edit_session_workflow {
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

FLASHMEM bool openForMacroInPage(core::state::sequencer::SequencerState& sequencer,
                                 SequencerHistoryDomainServices& history,
                                 oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                 uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!core::state::sequencer::resolveActiveContentStepInPage(sequencer, sequencer.page.get(),
                                                                indexInPage, abs)) {
        return false;
    }

    return openForStep(sequencer, history, overlays, abs);
}

FLASHMEM bool openForStep(core::state::sequencer::SequencerState& sequencer,
                          SequencerHistoryDomainServices& history,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          uint8_t step) {
    if (step >= core::state::sequencer::activeContentLength(sequencer)) { return false; }
    if (!commitHistory(history)) return false;

    sequencer.focusedStep.set(step);
    sequencer.page.set(core::state::sequencer::activeContentPageForStep(step));

    auto& edit = sequencer.stepEdit;
    edit.reset();
    edit.focusedRow.set(step_edit_rows::rowForNavigationIndex(0));
    edit.stepIndex.set(step);

    overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT);
    return true;
}

FLASHMEM bool commitHistory(SequencerHistoryDomainServices& history) {
    const auto familyOutcome = history.commitPreparedPatternEdit(
        core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession);
    if (familyOutcome ==
        core::state::sequencer::SequencerPreparedPatternEditCommitOutcome::Failed) {
        return false;
    }
    return history.commitCoalescedPatternEditOutcome() !=
           core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed;
}

FLASHMEM bool retargetRootStep(core::state::sequencer::SequencerState& sequencer,
                               SequencerHistoryDomainServices& history, int direction) {
    if (direction == 0 || !core::state::sequencer::isRootContentView(sequencer)) { return false; }
    const uint8_t length = core::state::sequencer::activeContentLength(sequencer);
    if (length == 0U) return false;

    const uint8_t current =
        std::min<uint8_t>(sequencer.stepEdit.stepIndex.get(), static_cast<uint8_t>(length - 1U));
    const uint8_t next = direction > 0
                             ? static_cast<uint8_t>((static_cast<uint16_t>(current) + 1U) % length)
                             : (current == 0U ? static_cast<uint8_t>(length - 1U)
                                              : static_cast<uint8_t>(current - 1U));
    if (next == current) return false;

    // Close both history aggregation layers before changing the target.  The
    // next snapshot therefore cannot absorb edits from the previous Step.
    if (!commitHistory(history)) return false;

    auto& edit = sequencer.stepEdit;
    edit.contextHold.clear();
    edit.localVariationEditActive.set(false);
    edit.chordEditor.reset();
    sequencer.focusedStep.set(next);
    sequencer.page.set(core::state::sequencer::activeContentPageForStep(next));
    edit.stepIndex.set(next);
    return true;
}

FLASHMEM bool backToParentContent(core::state::sequencer::SequencerState& sequencer,
                                  SequencerHistoryDomainServices& history) {
    if (!core::state::sequencer::isChildContentView(sequencer)) return false;

    uint8_t parentContextRow = sequencer.stepEdit.focusedRow.get();
    if (const auto* frame = sequencer.contentView.currentFrame()) {
        parentContextRow =
            frame->kind == core::state::sequencer::SequencerContentViewKind::MICRO_SEQUENCE
                ? step_edit_rows::MICRO_SEQUENCE
                : step_edit_rows::CYCLE_STATES;
    }

    if (!commitHistory(history)) return false;
    if (!core::state::sequencer::leaveContentView(sequencer)) return false;

    auto& edit = sequencer.stepEdit;
    edit.contextHold.clear();
    edit.localVariationEditActive.set(false);
    edit.stepIndex.set(sequencer.focusedStep.get());
    edit.focusedRow.set(parentContextRow);
    return true;
}

FLASHMEM bool close(core::state::sequencer::SequencerState& sequencer,
                    SequencerHistoryDomainServices& history,
                    ButtonReleaseLatch<2>& contextReleaseLatch,
                    oc::context::OverlayManager<core::ui::OverlayType>& overlays) {
    if (!commitHistory(history)) return false;
    contextReleaseLatch.clear();
    overlays.hide();
    sequencer.stepEdit.reset();
    return true;
}

FLASHMEM bool editedStepInRange(const core::state::sequencer::SequencerState& sequencer,
                                uint8_t& step) {
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer);
    if (len == 0) return false;

    step = sequencer.stepEdit.stepIndex.get();
    return step < len;
}

FLASHMEM bool shouldCloseFromMacro(const core::state::sequencer::SequencerState& sequencer,
                                   uint8_t indexInPage) {
    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t currentIndexInPage =
        static_cast<uint8_t>(sequencer.stepEdit.stepIndex.get() % stepsPerPage);
    return indexInPage == currentIndexInPage;
}

}  // namespace core::handler::sequencer::step_edit_session_workflow
