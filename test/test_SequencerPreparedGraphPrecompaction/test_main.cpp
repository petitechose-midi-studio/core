#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"

namespace {

namespace seq = core::state::sequencer;

using Action = core::handler::SequencerPreparedPageStructureAction;
using Execution = core::handler::SequencerPreparedPageStructureExecution;
using History = core::handler::SequencerHistoryDomainServices;
using MutationOutcome =
    core::handler::SequencerPreparedPageStructureMutationOutcome;
using Result = core::handler::SequencerPreparedPageStructureResult;
using Transaction =
    core::handler::SequencerPreparedPageStructureTransaction;

uint64_t byteHash(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    History history;
    uint16_t originalSequence = 0U;
    uint16_t originalFirstNode = 0U;

    Harness()
        : state(storages.settings),
          history(History::fromCoreState(state)) {
        (void)state.sequencer.pattern.setContentLength(8U);
        const auto first = seq::createMicroSequence(
            state.sequencer.pattern, seq::rootStepNodeId(0U), 2U);
        const auto current = seq::createMicroSequence(
            state.sequencer.pattern, seq::rootStepNodeId(1U), 2U);
        assert(first.ok && current.ok && first.id != current.id);
        originalSequence = current.id;

        const auto* graph = seq::graphView(state.sequencer.pattern);
        assert(graph != nullptr);
        const auto* sequence = graph->sequence(current.id);
        assert(sequence != nullptr);
        originalFirstNode = sequence->firstStepNode;

        assert(seq::initializeTrackBankFromActive(
            state.sequencerTracks, state.sequencer));
        assert(seq::enterMicroSequenceContentView(
            state.sequencer,
            seq::rootStepNodeId(1U),
            current.id));
        settle();
    }

    void settle() {
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        state.acknowledgeProjectSessionSave(
            state.project.metadata.modifiedCounter);
    }
};

struct Mutation {
    uint8_t expectedTrack = 0U;
    bool failAfterPrecompaction = false;
    bool revalidated = false;
    bool mutated = false;
    std::size_t finalizeCount = 0U;
    seq::SequencerPreparedGraphContentPath path{};
    seq::SequencerPreparedPatternGraphPrecompactionOutcome outcome =
        seq::SequencerPreparedPatternGraphPrecompactionOutcome::Failed;

    static bool revalidate(
        const void* context,
        const seq::SequencerState& sequencer
    ) noexcept {
        auto& self = *const_cast<Mutation*>(
            static_cast<const Mutation*>(context));
        self.revalidated = self.path.valid &&
            self.path.stackDepth == 1U &&
            sequencer.contentView.stackDepth == self.path.stackDepth;
        return self.revalidated;
    }

    static MutationOutcome apply(
        void* context,
        seq::SequencerState& sequencer,
        const History& history
    ) noexcept {
        auto& self = *static_cast<Mutation*>(context);
        auto* graph = sequencer.pattern.graph.get();
        if (graph == nullptr ||
            !seq::resetStepNodePayloadUnversioned(
                *graph, seq::rootStepNodeId(0U))) {
            return MutationOutcome::Failed;
        }

        self.outcome = history.precompactPreparedPatternEditGraph(
            seq::SequencerPreparedPatternEditOwner::PageStructure,
            static_cast<uint8_t>(Action::PageClear),
            self.expectedTrack,
            self.path);
        if (self.outcome !=
            seq::SequencerPreparedPatternGraphPrecompactionOutcome::Compacted) {
            return MutationOutcome::Failed;
        }
        self.mutated = true;
        if (self.failAfterPrecompaction) return MutationOutcome::Failed;

        const auto node = seq::preparedSequencerContentStepNodeId(
            sequencer, self.path, 0U);
        return node ==
                oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID
            ? MutationOutcome::Failed
            : MutationOutcome::Changed;
    }

    static void finalize(
        void* context,
        seq::SequencerState& sequencer
    ) noexcept {
        auto& self = *static_cast<Mutation*>(context);
        ++self.finalizeCount;
        seq::publishPreparedSequencerGraphContentPath(
            sequencer, self.path);
    }
};

Execution execution(Mutation& mutation) {
    return {
        .payloadPlan =
            seq::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload,
        .action = Action::PageClear,
        .expectedTrack = mutation.expectedTrack,
        .beforePageCount = 1,
        .afterPageCount = 1,
        .compactGraphOnSeal = true,
        .mutationContext = &mutation,
        .revalidate = &Mutation::revalidate,
        .mutate = &Mutation::apply,
        .finalizeCommitted = &Mutation::finalize,
    };
}

void test_precompaction_remaps_detached_path_and_publishes_after_commit() {
    Harness h;
    Mutation mutation;
    mutation.path = seq::capturePreparedSequencerGraphContentPath(
        h.state.sequencer);
    const uint32_t viewRevision = h.state.sequencer.contentView.revision.get();

    Transaction transaction(
        h.state.sequencer, h.history, Action::PageClear);
    assert(transaction.openBoundary());
    {
        // Four Full-Graph allocations are prepared. Ordinal five must remain
        // armed across release, compaction, seal, commit and UI publication.
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        assert(transaction.execute(execution(mutation)) == Result::Committed);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 5U);
    }

    assert(mutation.revalidated && mutation.mutated);
    assert(mutation.finalizeCount == 1U);
    assert(mutation.path.compacted);
    assert(mutation.path.frames[0].sequenceId < h.originalSequence);
    assert(h.state.sequencer.contentView.sequenceId.get() ==
           mutation.path.frames[0].sequenceId);
    assert(h.state.sequencer.contentView.currentFrame() != nullptr);
    assert(h.state.sequencer.contentView.currentFrame()->sequenceId ==
           mutation.path.frames[0].sequenceId);
    assert(seq::activeContentStepNodeId(h.state.sequencer, 0U) <
           h.originalFirstNode);
    assert(h.state.sequencer.contentView.revision.get() == viewRevision + 1U);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.projectHistory.undoCount() == 1U);

    std::cout <<
        "[PASS] precompaction remaps and publishes the detached path post-commit\n";
}

void test_failed_post_precompaction_mutation_aborts_without_ui_publication() {
    Harness h;
    auto* const graphOwner = h.state.sequencer.pattern.graph.get();
    assert(graphOwner != nullptr);
    const uint64_t graphHash = byteHash(graphOwner, sizeof(*graphOwner));
    const uint32_t graphRevision = h.state.sequencer.pattern.graphRevision.get();
    const uint32_t viewRevision = h.state.sequencer.contentView.revision.get();
    const auto frame = *h.state.sequencer.contentView.currentFrame();

    Mutation mutation{.failAfterPrecompaction = true};
    mutation.path = seq::capturePreparedSequencerGraphContentPath(
        h.state.sequencer);
    Transaction transaction(
        h.state.sequencer, h.history, Action::PageClear);
    assert(transaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        assert(transaction.execute(execution(mutation)) == Result::Failed);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 5U);
    }

    assert(mutation.revalidated && mutation.mutated);
    assert(mutation.finalizeCount == 0U);
    assert(h.state.sequencer.pattern.graph.get() == graphOwner);
    assert(byteHash(graphOwner, sizeof(*graphOwner)) == graphHash);
    assert(h.state.sequencer.pattern.graphRevision.get() == graphRevision);
    assert(h.state.sequencer.contentView.revision.get() == viewRevision);
    assert(h.state.sequencer.contentView.stackDepth == 1U);
    assert(h.state.sequencer.contentView.currentFrame() != nullptr);
    assert(h.state.sequencer.contentView.currentFrame()->ownerNodeId ==
           frame.ownerNodeId);
    assert(h.state.sequencer.contentView.currentFrame()->sequenceId ==
           frame.sequenceId);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout <<
        "[PASS] failed post-precompaction mutation aborts with exact Graph/UI\n";
}

}  // namespace

int main() {
    test_precompaction_remaps_detached_path_and_publishes_after_commit();
    test_failed_post_precompaction_mutation_aborts_without_ui_publication();
    std::cout << "All prepared Graph precompaction tests passed.\n";
    return 0;
}
