#include "handler/sequencer/SequencerStepEditSessionWorkflow.hpp"

#include <algorithm>
#include <utility>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler::sequencer::step_edit_session_workflow {
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

FLASHMEM bool openForMacroInPage(
    core::state::sequencer::SequencerState& sequencer,
    SequencerHistoryDomainServices& history,
    ButtonReleaseLatch<8>& openReleaseLatch,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    StepEditHistorySnapshot& historySnapshot,
    bool& historySnapshotValid,
    uint8_t indexInPage
) {
    history.commitCoalescedPatternEdit();

    uint8_t abs = 0;
    if (!core::state::sequencer::resolveActiveContentStepInPage(
            sequencer,
            sequencer.page.get(),
            indexInPage,
            abs
        )) {
        return false;
    }

    historySnapshotValid =
        core::state::sequencer::captureHistorySnapshot(sequencer, historySnapshot);

    sequencer.focusedStep.set(abs);

    auto& edit = sequencer.stepEdit;
    edit.reset();
    edit.focusedRow.set(step_edit_rows::rowForNavigationIndex(0));
    edit.stepIndex.set(abs);

    openReleaseLatch.arm(Config::MACRO_BUTTONS[indexInPage]);
    overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT);
    return true;
}

FLASHMEM bool commitHistory(
    core::state::sequencer::SequencerState& sequencer,
    SequencerHistoryDomainServices& history,
    StepEditHistorySnapshot& historySnapshot,
    bool& historySnapshotValid
) {
    bool recorded = false;
    if (historySnapshotValid) {
        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        const bool graphUnchanged =
            historySnapshot.flat.graphRevision == sequencer.pattern.graphRevision.get();
        bool captured = true;
        if (graphUnchanged) {
            historySnapshot.graph.reset();
            core::state::sequencer::captureFlatHistorySnapshot(sequencer, after);
        } else {
            captured = core::state::sequencer::captureHistorySnapshot(sequencer, after);
        }

        if (captured &&
            !core::state::sequencer::sameMusicalHistorySnapshot(historySnapshot, after)) {
            const auto descriptor = core::state::sequencer::SequencerHistoryDescriptor{
                .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
                .stepIndex = sequencer.stepEdit.stepIndex.get(),
            };
            recorded = graphUnchanged
                ? history.recordFlatPattern(
                      std::move(historySnapshot),
                      std::move(after),
                      descriptor
                  )
                : history.recordPattern(
                      std::move(historySnapshot),
                      std::move(after),
                      descriptor
                  );
        }
    }

    historySnapshotValid = false;
    return recorded;
}

FLASHMEM bool retargetRootStep(
    core::state::sequencer::SequencerState& sequencer,
    SequencerHistoryDomainServices& history,
    StepEditHistorySnapshot& historySnapshot,
    bool& historySnapshotValid,
    int direction
) {
    if (direction == 0 ||
        !core::state::sequencer::isRootContentView(sequencer)) {
        return false;
    }
    const uint8_t length = core::state::sequencer::activeContentLength(sequencer);
    if (length == 0U) return false;

    const uint8_t current = std::min<uint8_t>(
        sequencer.stepEdit.stepIndex.get(),
        static_cast<uint8_t>(length - 1U)
    );
    const uint8_t next = direction > 0
        ? static_cast<uint8_t>((static_cast<uint16_t>(current) + 1U) % length)
        : (current == 0U ? static_cast<uint8_t>(length - 1U)
                         : static_cast<uint8_t>(current - 1U));
    if (next == current) return false;

    // Close both history aggregation layers before changing the target.  The
    // next snapshot therefore cannot absorb edits from the previous Step.
    commitHistory(sequencer, history, historySnapshot, historySnapshotValid);
    history.commitCoalescedPatternEdit();

    auto& edit = sequencer.stepEdit;
    edit.contextHold.clear();
    edit.localVariationEditActive.set(false);
    edit.chordEditor.reset();
    sequencer.focusedStep.set(next);
    sequencer.page.set(core::state::sequencer::activeContentPageForStep(next));
    edit.stepIndex.set(next);
    historySnapshotValid =
        core::state::sequencer::captureHistorySnapshot(sequencer, historySnapshot);
    return true;
}

FLASHMEM bool backToParentContent(
    core::state::sequencer::SequencerState& sequencer,
    SequencerHistoryDomainServices& history,
    StepEditHistorySnapshot& historySnapshot,
    bool& historySnapshotValid
) {
    if (!core::state::sequencer::isChildContentView(sequencer)) return false;

    uint8_t parentContextRow = sequencer.stepEdit.focusedRow.get();
    if (const auto* frame = sequencer.contentView.currentFrame()) {
        parentContextRow =
            frame->kind == core::state::sequencer::SequencerContentViewKind::MICRO_SEQUENCE
                ? step_edit_rows::MICRO_SEQUENCE
                : step_edit_rows::CYCLE_STATES;
    }

    commitHistory(sequencer, history, historySnapshot, historySnapshotValid);
    history.commitCoalescedPatternEdit();
    if (!core::state::sequencer::leaveContentView(sequencer)) return false;

    auto& edit = sequencer.stepEdit;
    edit.contextHold.clear();
    edit.localVariationEditActive.set(false);
    edit.stepIndex.set(sequencer.focusedStep.get());
    edit.focusedRow.set(parentContextRow);
    historySnapshotValid =
        core::state::sequencer::captureHistorySnapshot(sequencer, historySnapshot);
    return true;
}

FLASHMEM void close(
    core::state::sequencer::SequencerState& sequencer,
    SequencerHistoryDomainServices& history,
    ButtonReleaseLatch<8>& openReleaseLatch,
    ButtonReleaseLatch<2>& contextReleaseLatch,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    StepEditHistorySnapshot& historySnapshot,
    bool& historySnapshotValid
) {
    commitHistory(sequencer, history, historySnapshot, historySnapshotValid);
    openReleaseLatch.clear();
    contextReleaseLatch.clear();
    overlays.hide();
    sequencer.stepEdit.reset();
}

FLASHMEM bool editedStepInRange(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t& step
) {
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer);
    if (len == 0) return false;

    step = sequencer.stepEdit.stepIndex.get();
    return step < len;
}

FLASHMEM bool shouldCloseFromMacro(
    ButtonReleaseLatch<8>& openReleaseLatch,
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t indexInPage
) {
    if (openReleaseLatch.consume(Config::MACRO_BUTTONS[indexInPage])) {
        return false;
    }

    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
    const uint8_t currentIndexInPage =
        static_cast<uint8_t>(sequencer.stepEdit.stepIndex.get() % stepsPerPage);
    return indexInPage == currentIndexInPage;
}

}  // namespace core::handler::sequencer::step_edit_session_workflow
