#include "handler/sequencer/SequencerStepContextRowWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler::sequencer::step_context_row_workflow {
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

namespace {

FLASHMEM core::state::sequencer::StepContentChildKind focusedChildKind(
    const core::state::sequencer::SequencerState& sequencer
) {
    return step_edit_rows::childKindForContextRow(sequencer.stepEdit.focusedRow.get());
}

FLASHMEM uint8_t defaultLengthForChildKind(
    core::state::sequencer::StepContentChildKind childKind
) {
    return childKind == core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
        ? core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
        : core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT;
}

}  // namespace

FLASHMEM bool focusedRowIsContext(
    const core::state::sequencer::SequencerState& sequencer
) {
    return step_edit_rows::isContext(sequencer.stepEdit.focusedRow.get());
}

FLASHMEM bool focusedContextHasChild(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step
) {
    if (!focusedRowIsContext(sequencer)) return false;
    return core::state::sequencer::activeContentStepHasChildContent(
        sequencer,
        step,
        focusedChildKind(sequencer)
    );
}

FLASHMEM bool canPasteFocusedContextChild(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::StructureClipboardState& clipboard
) {
    if (!focusedRowIsContext(sequencer)) return false;
    const auto childKind = focusedChildKind(sequencer);
    return core::state::sequencer::clipboardCanPasteActiveContentChild(
               clipboard,
               childKind
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               sequencer,
               step
           );
}

FLASHMEM core::state::sequencer::StepContentOpenResult openOrCreateFocusedContextChild(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step
) {
    if (!focusedRowIsContext(sequencer)) return {};
    const auto childKind = focusedChildKind(sequencer);
    return core::state::sequencer::openOrCreateActiveContentChild(
        sequencer,
        step,
        childKind,
        defaultLengthForChildKind(childKind)
    );
}

FLASHMEM bool copyFocusedContextChildToClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::StructureClipboardState& clipboard
) {
    if (!focusedContextHasChild(sequencer, step)) return false;
    return core::state::sequencer::copyActiveContentChildToClipboard(
        sequencer,
        step,
        focusedChildKind(sequencer),
        clipboard
    );
}

FLASHMEM bool clearFocusedContextChild(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step
) {
    if (!focusedContextHasChild(sequencer, step)) return false;
    return core::state::sequencer::clearActiveContentChild(
        sequencer,
        step,
        focusedChildKind(sequencer)
    );
}

FLASHMEM bool pasteFocusedContextChildFromClipboard(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::StructureClipboardState& clipboard
) {
    if (!canPasteFocusedContextChild(sequencer, step, clipboard)) return false;
    return core::state::sequencer::pasteActiveContentChildFromClipboard(
        sequencer,
        step,
        focusedChildKind(sequencer),
        clipboard
    );
}

}  // namespace core::handler::sequencer::step_context_row_workflow
