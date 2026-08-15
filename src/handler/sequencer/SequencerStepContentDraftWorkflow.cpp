#include "handler/sequencer/SequencerStepContentDraftWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::handler::sequencer::step_content_draft_workflow {

namespace {

FLASHMEM void noteFailure(core::state::sequencer::SequencerState& sequencer,
                          core::state::sequencer::SequencerStepContentDraftFailure failure) {
    namespace seq = core::state::sequencer;
    sequencer.stepContentDraft.noteFailure(failure);
    if (failure == seq::SequencerStepContentDraftFailure::OUT_OF_MEMORY) {
        sequencer.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::ResourceUnavailable, core::time_compat::millis());
    } else if (failure == seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE) {
        sequencer.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
    }
}

}  // namespace

FLASHMEM bool apply(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::handler::SequencerHistoryDomainServices& history
) {
    namespace seq = core::state::sequencer;
    if (!sequencer.stepContentDraft.active.get()) return false;
    sequencer.stepContentDraft.clearFailure();
    if (!seq::stepContentDraftHasPublishableSubset(sequencer)) {
        noteFailure(sequencer, seq::SequencerStepContentDraftFailure::UNPUBLISHABLE_MUTATION
        );
        return false;
    }

    if (seq::isDrumContentView(sequencer)) {
        const auto& owner = sequencer.contentView;
        const seq::SequencerHistoryDescriptor descriptor{
            .kind = seq::SequencerHistoryActionKind::DrumAdvancedContent,
            .trackIndex = owner.drumOwnerTrack,
            .laneIndex = owner.drumOwnerLane,
            .stepIndex = owner.drumOwnerStep,
            .property = seq::StepProperty::NOTE,
        };
        if (!seq::publishStepContentDraft(sequencer)) {
            noteFailure(
                sequencer,
                seq::SequencerStepContentDraftFailure::UNPUBLISHABLE_MUTATION
            );
            return false;
        }
        tracks.publishDrumMutation(owner.drumOwnerTrack);
        if (!history.sealCoalescedDrumEdit(true, descriptor)) {
            (void)history.abortCoalescedDrumEdit();
            sequencer.contentView.reset();
            noteFailure(
                sequencer,
                seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE
            );
            return false;
        }
        if (history.commitCoalescedDrumEditOutcome() ==
            seq::SequencerPatternHistoryCommitOutcome::Failed) {
            sequencer.contentView.reset();
            noteFailure(
                sequencer,
                seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE
            );
            return false;
        }
        return true;
    }

    auto change = core::app::makeExtmemUnique<seq::SequencerHistoryPatternChange>();
    if (!change) {
        noteFailure(sequencer, seq::SequencerStepContentDraftFailure::OUT_OF_MEMORY
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
        noteFailure(sequencer, seq::SequencerStepContentDraftFailure::OUT_OF_MEMORY
        );
        return false;
    }
    if (!history.canRecordPattern(*change)) {
        noteFailure(sequencer, seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE
        );
        return false;
    }

    // The active editor and its Track-bank mirror must cross the publication
    // barrier together. Prepare the exact post-draft cold payload first so a
    // later FlatOnly edit cannot observe a stale Graph/CC mirror.
    seq::SequencerPreparedActiveTrackSynchronization trackSynchronization;
    if (!seq::prepareActiveTrackSynchronizationFromSnapshot(
            tracks, change->trackIndex, change->after, trackSynchronization)) {
        noteFailure(sequencer, seq::SequencerStepContentDraftFailure::OUT_OF_MEMORY);
        return false;
    }

    if (!seq::publishStepContentDraft(sequencer)) {
        noteFailure(sequencer, seq::SequencerStepContentDraftFailure::UNPUBLISHABLE_MUTATION);
        return false;
    }
    seq::publishPreparedActiveTrackSynchronization(
        tracks, sequencer, change->after, std::move(trackSynchronization));
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

FLASHMEM BackResult requestBack(
    core::state::sequencer::SequencerState& sequencer,
    const core::handler::SequencerHistoryDomainServices& history
) {
    namespace seq = core::state::sequencer;
    if (!sequencer.stepContentDraft.active.get()) return BackResult::NONE;
    if (sequencer.stepContentDraft.modified()) {
        sequencer.stepContentDraft.showExitPrompt();
        return BackResult::CONTINUE_EDITING;
    }
    if (seq::isDrumContentView(sequencer) &&
        !history.abortCoalescedDrumEdit()) {
        noteFailure(
            sequencer,
            seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE
        );
        return BackResult::FAILED;
    }
    seq::abandonStepContentDraft(sequencer);
    return BackResult::DISCARDED;
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
    core::state::sequencer::SequencerTrackBankState& tracks,
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
            if (seq::isDrumContentView(sequencer) &&
                !history.abortCoalescedDrumEdit()) {
                noteFailure(
                    sequencer,
                    seq::SequencerStepContentDraftFailure::HISTORY_UNAVAILABLE
                );
                return BackResult::FAILED;
            }
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
