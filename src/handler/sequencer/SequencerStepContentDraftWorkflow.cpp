#include "handler/sequencer/SequencerStepContentDraftWorkflow.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::handler::sequencer::step_content_draft_workflow {

FLASHMEM bool apply(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::handler::SequencerHistoryDomainServices& history
) {
    namespace seq = core::state::sequencer;
    if (!sequencer.stepContentDraft.active.get()) return false;
    sequencer.stepContentDraft.clearFailure();
    if (!seq::stepContentDraftHasPublishableSubset(sequencer)) {
        sequencer.stepContentDraft.noteFailure(
            seq::SequencerStepContentDraftFailure::UNPUBLISHABLE_MUTATION
        );
        return false;
    }

    auto change = core::app::makeExtmemUnique<seq::SequencerHistoryPatternChange>();
    if (!change) {
        sequencer.stepContentDraft.noteFailure(
            seq::SequencerStepContentDraftFailure::OUT_OF_MEMORY
        );
        return false;
    }
    change->trackIndex = tracks.activeTrackIndex();
    change->storage = seq::SequencerHistoryPatternStorage::FullGraph;
    change->descriptor = {
        .kind = seq::SequencerHistoryActionKind::StepEdit,
        .trackIndex = tracks.activeTrackIndex(),
        .stepIndex = sequencer.stepContentDraft.ownerStep,
        .property = seq::StepProperty::NOTE,
        .hasValue = false,
    };

    // Reserve every retained object and prove admission before publication.
    // capture(after) also retains the unchanged CC payload; only its graph is
    // then replaced with the prospective draft graph.
    if (!seq::captureHistorySnapshot(sequencer, change->before) ||
        !seq::captureHistorySnapshot(sequencer, change->after) ||
        !seq::captureStepContentDraftAfterSnapshot(sequencer, change->after)) {
        sequencer.stepContentDraft.noteFailure(
            seq::SequencerStepContentDraftFailure::OUT_OF_MEMORY
        );
        return false;
    }
    if (!history.canRecordPattern(*change)) {
        sequencer.stepContentDraft.noteFailure(
            seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE
        );
        return false;
    }

    if (!seq::publishStepContentDraft(sequencer)) return false;
    history.recordPreparedPattern(std::move(change));
    return true;
}

FLASHMEM BackResult requestBack(
    core::state::sequencer::SequencerState& sequencer
) {
    namespace seq = core::state::sequencer;
    if (!sequencer.stepContentDraft.active.get()) return BackResult::NONE;
    if (!sequencer.stepContentDraft.modified()) {
        seq::abandonStepContentDraft(sequencer);
        return BackResult::DISCARDED;
    }
    sequencer.stepContentDraft.showExitPrompt();
    return BackResult::CONTINUE_EDITING;
}

FLASHMEM void moveExitChoice(
    core::state::sequencer::SequencerState& sequencer,
    float delta
) {
    namespace seq = core::state::sequencer;
    if (!sequencer.stepContentDraft.exitPromptVisible.get() ||
        !nav::hasTurnDelta(delta)) {
        return;
    }
    const int current = static_cast<int>(
        sequencer.stepContentDraft.exitChoice.get()
    );
    const int next = nav::nextWrappedIndex(
        delta,
        current,
        static_cast<int>(seq::SequencerStepContentDraftExitChoice::COUNT)
    );
    sequencer.stepContentDraft.exitChoice.set(
        static_cast<seq::SequencerStepContentDraftExitChoice>(next)
    );
    sequencer.stepContentDraft.touch();
}

FLASHMEM BackResult applyExitChoice(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::handler::SequencerHistoryDomainServices& history
) {
    namespace seq = core::state::sequencer;
    if (!sequencer.stepContentDraft.exitPromptVisible.get()) {
        return BackResult::NONE;
    }

    switch (sequencer.stepContentDraft.exitChoice.get()) {
        case seq::SequencerStepContentDraftExitChoice::CONTINUE:
            sequencer.stepContentDraft.hideExitPrompt();
            return BackResult::CONTINUE_EDITING;
        case seq::SequencerStepContentDraftExitChoice::DISCARD:
            seq::abandonStepContentDraft(sequencer);
            return BackResult::DISCARDED;
        case seq::SequencerStepContentDraftExitChoice::SAVE:
            return apply(sequencer, tracks, history)
                ? BackResult::SAVED
                : BackResult::FAILED;
        case seq::SequencerStepContentDraftExitChoice::COUNT:
        default:
            return BackResult::FAILED;
    }
}

}  // namespace core::handler::sequencer::step_content_draft_workflow
