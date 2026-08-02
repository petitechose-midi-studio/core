#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

namespace allocation_trace {

constexpr std::size_t MAX_REQUESTS = 8U;
bool enabled = false;
std::array<std::size_t, MAX_REQUESTS> requests{};
std::size_t count = 0U;
bool overflow = false;

void record(std::size_t bytes) {
    if (!enabled) return;
    if (count >= requests.size()) {
        overflow = true;
        return;
    }
    requests[count] = bytes;
    ++count;
}

class Scope {
public:
    Scope() {
        requests.fill(0U);
        count = 0U;
        overflow = false;
        enabled = true;
    }

    ~Scope() { enabled = false; }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace allocation_trace

void* operator new(std::size_t bytes) {
    allocation_trace::record(bytes);
    if (void* memory = std::malloc(bytes)) return memory;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }

void operator delete(void* memory) noexcept { std::free(memory); }

void operator delete[](void* memory) noexcept { ::operator delete(memory); }

void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete[](memory);
}

namespace {

namespace seq = core::state::sequencer;
namespace tx = test_support::sequencer_transaction;

uint64_t byteHash(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

using Action = core::handler::SequencerPreparedPageStructureAction;
using Result = core::handler::SequencerPreparedPageStructureResult;
using MutationOutcome =
    core::handler::SequencerPreparedPageStructureMutationOutcome;
using Execution = core::handler::SequencerPreparedPageStructureExecution;
using Transaction = core::handler::SequencerPreparedPageStructureTransaction;
using Services = core::handler::SequencerHistoryDomainServices;
using BeginOutcome = seq::SequencerPreparedPatternEditBeginOutcome;
using SealOutcome = seq::SequencerPreparedPatternEditSealOutcome;
using CommitOutcome = seq::SequencerPreparedPatternEditCommitOutcome;
using AbortOutcome = seq::SequencerPreparedPatternEditAbortOutcome;
using BoundaryOutcome = seq::SequencerPatternHistoryCommitOutcome;
using Plan = seq::SequencerCoalescedPatternPayloadPlan;
using Owner = seq::SequencerPreparedPatternEditOwner;

constexpr std::size_t kArmAllocationHeaderBytes = 16U;
constexpr std::size_t kArmPatternChangeBytes = 1736U;
constexpr std::size_t kArmGraphBytes = 14792U;
constexpr std::size_t kArmCcBankBytes = 840U;

static_assert(
    kArmPatternChangeBytes +
            3U * (kArmGraphBytes + kArmCcBankBytes) +
            7U * kArmAllocationHeaderBytes ==
        48744U,
    "LOCK-P maximum Page transaction peak changed"
);
static_assert(
    kArmPatternChangeBytes + 2U * kArmGraphBytes +
            3U * kArmCcBankBytes + 6U * kArmAllocationHeaderBytes ==
        33936U,
    "LOCK-P disabled-to-enabled Graph plus CC peak changed"
);
static_assert(
    kArmPatternChangeBytes +
            2U * (kArmGraphBytes + kArmCcBankBytes) +
            5U * kArmAllocationHeaderBytes ==
        33080U,
    "LOCK-P retained maximal Page entry changed"
);

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(
    sizeof(seq::SequencerHistoryPatternChange) == kArmPatternChangeBytes
);
static_assert(
    sizeof(oc::note::sequencer::StepSequencerGraph) == kArmGraphBytes
);
static_assert(sizeof(seq::SequencerCcLaneBank) == kArmCcBankBytes);
#endif

// Native request-size oracles for the frozen H,C,G,C,G,C disabled-Graph
// variant and the H,G,C,G,C,G,C maximal enabled-Graph variant.
constexpr std::array<std::size_t, 6U>
    kDisabledGraphToEnabledWithCcRequests{
        sizeof(seq::SequencerHistoryPatternChange),
        sizeof(seq::SequencerCcLaneBank),
        sizeof(oc::note::sequencer::StepSequencerGraph),
        sizeof(seq::SequencerCcLaneBank),
        sizeof(oc::note::sequencer::StepSequencerGraph),
        sizeof(seq::SequencerCcLaneBank),
    };

constexpr std::array<std::size_t, 7U> kEnabledGraphWithCcRequests{
    sizeof(seq::SequencerHistoryPatternChange),
    sizeof(oc::note::sequencer::StepSequencerGraph),
    sizeof(seq::SequencerCcLaneBank),
    sizeof(oc::note::sequencer::StepSequencerGraph),
    sizeof(seq::SequencerCcLaneBank),
    sizeof(oc::note::sequencer::StepSequencerGraph),
    sizeof(seq::SequencerCcLaneBank),
};

const char* executablePath = nullptr;

static_assert(!std::is_copy_constructible_v<Transaction>);
static_assert(!std::is_copy_assignable_v<Transaction>);
static_assert(!std::is_move_constructible_v<Transaction>);
static_assert(!std::is_move_assignable_v<Transaction>);

static_assert(static_cast<uint8_t>(Action::Invalid) == 0U);
static_assert(static_cast<uint8_t>(Action::PageSelectionPaste) == 1U);
static_assert(static_cast<uint8_t>(Action::PageClear) == 2U);
static_assert(static_cast<uint8_t>(Action::PageDelete) == 3U);
static_assert(static_cast<uint8_t>(Action::PagePaste) == 4U);
static_assert(static_cast<uint8_t>(Action::StepPaste) == 5U);
static_assert(static_cast<uint8_t>(Action::FocusedStepReset) == 6U);
static_assert(static_cast<uint8_t>(Action::StepSelectionReset) == 7U);
static_assert(static_cast<uint8_t>(Action::PageSelectionReset) == 8U);
static_assert(
    static_cast<uint8_t>(Action::PageSelectionDeleteOrDeepReset) == 9U
);

enum class Call : uint8_t {
    Boundary = 0,
    Begin,
    Revalidate,
    Ready,
    Mutation,
    Seal,
    Commit,
    Finalize,
    Abort,
};

struct LifecycleScript {
    BoundaryOutcome boundaryOutcome = BoundaryOutcome::NoPending;
    BeginOutcome beginOutcome = BeginOutcome::Started;
    SealOutcome sealOutcome = SealOutcome::Sealed;
    CommitOutcome commitOutcome = CommitOutcome::Committed;
    AbortOutcome abortOutcome = AbortOutcome::Aborted;
    bool readyOutcome = true;
    bool revalidateOutcome = true;
    MutationOutcome mutationOutcome = MutationOutcome::Changed;

    std::array<Call, 9U> calls{};
    std::size_t callCount = 0U;
    std::size_t boundaryCount = 0U;
    std::size_t beginCount = 0U;
    std::size_t readyCount = 0U;
    std::size_t revalidateCount = 0U;
    std::size_t mutationCount = 0U;
    std::size_t sealCount = 0U;
    std::size_t commitCount = 0U;
    std::size_t finalizeCount = 0U;
    std::size_t abortCount = 0U;
    bool mutationReceivedHistory = false;

    Owner beginOwner = Owner::PatternPitch;
    Owner sealOwner = Owner::PatternPitch;
    Owner commitOwner = Owner::PatternPitch;
    Owner abortOwner = Owner::PatternPitch;
    uint8_t beginKey = 0xFFU;
    uint8_t sealKey = 0xFFU;
    uint8_t abortKey = 0xFFU;
    uint8_t readyKey = 0xFFU;
    uint8_t readyTrack = 0xFFU;
    Owner readyOwner = Owner::PatternPitch;
    Plan payloadPlan = Plan::FlatOnly;
    bool compactGraphOnSeal = false;
    bool mutationChanged = false;
    seq::SequencerHistoryDescriptor beginDescriptor{};
    seq::SequencerHistoryDescriptor sealDescriptor{};

    void note(Call call) {
        assert(callCount < calls.size());
        calls[callCount++] = call;
    }

    static BoundaryOutcome boundary(void* context) {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Boundary);
        ++self.boundaryCount;
        return self.boundaryOutcome;
    }

    static BeginOutcome begin(
        void* context,
        Owner owner,
        uint8_t key,
        Plan payloadPlan,
        seq::SequencerHistoryDescriptor descriptor,
        bool compactGraphOnSeal
    ) {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Begin);
        ++self.beginCount;
        self.beginOwner = owner;
        self.beginKey = key;
        self.payloadPlan = payloadPlan;
        self.beginDescriptor = descriptor;
        self.compactGraphOnSeal = compactGraphOnSeal;
        return self.beginOutcome;
    }

    static SealOutcome seal(
        void* context,
        Owner owner,
        uint8_t key,
        bool mutationChanged,
        seq::SequencerHistoryDescriptor descriptor
    ) {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Seal);
        ++self.sealCount;
        self.sealOwner = owner;
        self.sealKey = key;
        self.mutationChanged = mutationChanged;
        self.sealDescriptor = descriptor;
        return self.sealOutcome;
    }

    static bool ready(
        void* context,
        Owner owner,
        uint8_t key,
        uint8_t expectedTrack
    ) {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Ready);
        ++self.readyCount;
        self.readyOwner = owner;
        self.readyKey = key;
        self.readyTrack = expectedTrack;
        return self.readyOutcome;
    }

    static bool revalidate(
        const void* context,
        const seq::SequencerState&
    ) noexcept {
        auto& self = *const_cast<LifecycleScript*>(
            static_cast<const LifecycleScript*>(context));
        self.note(Call::Revalidate);
        ++self.revalidateCount;
        return self.revalidateOutcome;
    }

    static MutationOutcome mutate(
        void* context,
        seq::SequencerState&,
        const Services& history
    ) noexcept {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Mutation);
        ++self.mutationCount;
        (void)history;
        self.mutationReceivedHistory = true;
        return self.mutationOutcome;
    }

    static CommitOutcome commit(void* context, Owner owner) {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Commit);
        ++self.commitCount;
        self.commitOwner = owner;
        return self.commitOutcome;
    }

    static void finalize(
        void* context,
        seq::SequencerState&
    ) noexcept {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Finalize);
        ++self.finalizeCount;
    }

    static AbortOutcome abort(void* context, Owner owner, uint8_t key) {
        auto& self = *static_cast<LifecycleScript*>(context);
        self.note(Call::Abort);
        ++self.abortCount;
        self.abortOwner = owner;
        self.abortKey = key;
        return self.abortOutcome;
    }
};

constexpr Services::Operations kLifecycleOperations{
    .commitCoalescedPatternEdit = &LifecycleScript::boundary,
    .beginPreparedPatternEdit = &LifecycleScript::begin,
    .preparedPatternEditReady = &LifecycleScript::ready,
    .sealPreparedPatternEdit = &LifecycleScript::seal,
    .commitPreparedPatternEdit = &LifecycleScript::commit,
    .abortPreparedPatternEdit = &LifecycleScript::abort,
};

Services services(LifecycleScript& script) {
    return Services::fromStaticOperations<kLifecycleOperations>(&script);
}

struct Harness {
    seq::SequencerState sequencer;
    LifecycleScript script;
    Services history;

    Harness()
        : history(services(script)) {}
};

constexpr std::array<Action, 9U> kActions{
    Action::PageSelectionPaste,
    Action::PageClear,
    Action::PageDelete,
    Action::PagePaste,
    Action::StepPaste,
    Action::FocusedStepReset,
    Action::StepSelectionReset,
    Action::PageSelectionReset,
    Action::PageSelectionDeleteOrDeepReset,
};

bool sameDescriptor(
    const seq::SequencerHistoryDescriptor& lhs,
    const seq::SequencerHistoryDescriptor& rhs
) {
    return lhs.kind == rhs.kind &&
           lhs.trackIndex == rhs.trackIndex &&
           lhs.laneIndex == rhs.laneIndex &&
           lhs.stepIndex == rhs.stepIndex &&
           lhs.property == rhs.property &&
           lhs.hasValue == rhs.hasValue &&
           lhs.beforeValue == rhs.beforeValue &&
           lhs.afterValue == rhs.afterValue;
}

void assertCallSequence(
    const LifecycleScript& script,
    std::initializer_list<Call> expected
) {
    assert(script.callCount == expected.size());
    std::size_t index = 0U;
    for (const Call call : expected) {
        assert(script.calls[index++] == call);
    }
}

void assertAllocationCount(std::size_t expected) {
    assert(!allocation_trace::overflow);
    assert(allocation_trace::count == expected);
}

template <std::size_t N>
void assertAllocationRequestPrefix(
    const std::array<std::size_t, N>& expected,
    std::size_t count
) {
    assert(!allocation_trace::overflow);
    assert(count <= expected.size());
    assert(allocation_trace::count == count);
    for (std::size_t index = 0U; index < count; ++index) {
        assert(allocation_trace::requests[index] == expected[index]);
    }
}

Execution execution(
    LifecycleScript& script,
    Plan plan = Plan::FullCurrentPayload,
    bool compactGraphOnSeal = true,
    uint8_t track = 3U,
    int32_t beforePages = 2,
    int32_t afterPages = 4,
    Action action = Action::PageClear
) {
    return {
        .payloadPlan = plan,
        .action = action,
        .expectedTrack = track,
        .beforePageCount = beforePages,
        .afterPageCount = afterPages,
        .compactGraphOnSeal = compactGraphOnSeal,
        .mutationContext = &script,
        .revalidate = &LifecycleScript::revalidate,
        .mutate = &LifecycleScript::mutate,
        .finalizeCommitted = &LifecycleScript::finalize,
    };
}

void test_exact_nine_actions_forward_stable_owner_keys() {
    for (std::size_t index = 0U; index < kActions.size(); ++index) {
        Harness h;
        h.script.sealOutcome = SealOutcome::Cleared;
        h.script.mutationOutcome = MutationOutcome::NoChange;
        {
            Transaction transaction(h.sequencer, h.history, kActions[index]);
            assert(transaction.openBoundary());
            assert(transaction.execute(execution(
                       h.script,
                       Plan::FullCurrentPayload,
                       true,
                       3U,
                       2,
                       4,
                       kActions[index])) == Result::NoChange);
        }

        assertCallSequence(
            h.script,
            {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
             Call::Mutation, Call::Seal}
        );
        assert(h.script.beginOwner == Owner::PageStructure);
        assert(h.script.readyOwner == Owner::PageStructure);
        assert(h.script.sealOwner == Owner::PageStructure);
        const auto expectedKey = static_cast<uint8_t>(kActions[index]);
        assert(h.script.beginKey == expectedKey);
        assert(h.script.readyKey == expectedKey);
        assert(h.script.readyTrack == 3U);
        assert(h.script.sealKey == expectedKey);
        assert(h.script.payloadPlan == Plan::FullCurrentPayload);
        assert(h.script.compactGraphOnSeal);
        assert(!h.script.mutationChanged);
        assert(h.script.beginDescriptor.kind ==
               seq::SequencerHistoryActionKind::PageStructure);
        assert(h.script.beginDescriptor.trackIndex == 3U);
        assert(h.script.beginDescriptor.hasValue);
        assert(h.script.beginDescriptor.beforeValue == 2);
        assert(h.script.beginDescriptor.afterValue == 4);
        assert(sameDescriptor(
            h.script.beginDescriptor,
            h.script.sealDescriptor
        ));
        assert(h.script.commitCount == 0U);
        assert(h.script.abortCount == 0U);
    }

    std::cout << "[PASS] exact nine Page actions forward stable owner/key identities\n";
}

void test_draft_rejects_before_the_only_boundary() {
    Harness h;
    h.sequencer.stepContentDraft.active.set(true);
    const uint32_t revisionBefore = h.sequencer.stepContentDraft.revision.get();
    const uint32_t contentRevisionBefore = h.sequencer.contentView.revision.get();
    {
        Transaction transaction(h.sequencer, h.history, Action::PageClear);
        assert(!transaction.openBoundary());
    }

    assert(h.script.callCount == 0U);
    assert(h.sequencer.stepContentDraft.active.get());
    assert(h.sequencer.stepContentDraft.failure ==
           seq::SequencerStepContentDraftFailure::TRANSITION_BLOCKED);
    assert(h.sequencer.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::STRUCTURE_EDIT);
    assert(h.sequencer.stepContentDraft.revision.get() == revisionBefore + 1U);
    assert(h.sequencer.contentView.revision.get() == contentRevisionBefore + 1U);

    std::cout << "[PASS] Draft rejection precedes the Page Pattern boundary\n";
}

void test_boundary_failure_and_one_shot_protocol_stop_before_begin() {
    {
        Harness h;
        h.script.boundaryOutcome = BoundaryOutcome::Failed;
        Transaction transaction(h.sequencer, h.history, Action::PageClear);
        assert(!transaction.openBoundary());
        assert(transaction.execute(execution(h.script)) == Result::Failed);
        assertCallSequence(h.script, {Call::Boundary});
    }

    {
        Harness h;
        h.script.boundaryOutcome = BoundaryOutcome::Committed;
        Transaction transaction(h.sequencer, h.history, Action::PageClear);
        assert(transaction.openBoundary());
        assert(!transaction.openBoundary());
        assert(transaction.execute(execution(h.script)) == Result::Failed);
        assertCallSequence(h.script, {Call::Boundary});
    }

    {
        Harness h;
        Transaction invalidAction(
            h.sequencer,
            h.history,
            Action::Invalid
        );
        assert(invalidAction.openBoundary());
        assert(invalidAction.execute(
                   execution(
                       h.script,
                       Plan::FlatOnly,
                       false,
                       0U,
                       1,
                       1,
                       Action::Invalid)) ==
               Result::Failed);
        assertCallSequence(h.script, {Call::Boundary});
    }

    {
        Harness h;
        Transaction invalidTrack(h.sequencer, h.history, Action::PageDelete);
        assert(invalidTrack.openBoundary());
        assert(invalidTrack.execute(execution(
                   h.script,
                   Plan::FlatOnly,
                   false,
                   seq::SequencerTrackBankState::TRACK_COUNT,
                   1,
                   1,
                   Action::PageDelete
               )) == Result::Failed);
        assertCallSequence(h.script, {Call::Boundary});
    }

    std::cout << "[PASS] boundary and begin phases are one-shot and fail closed\n";
}

void test_begin_accepts_started_only_and_closes_continued() {
    {
        Harness h;
        h.script.beginOutcome = BeginOutcome::Failed;
        Transaction transaction(h.sequencer, h.history, Action::StepPaste);
        assert(transaction.openBoundary());
        assert(transaction.execute(execution(
                   h.script,
                   Plan::FullWithProspectiveGraph,
                   false,
                   2U,
                   1,
                   1,
                   Action::StepPaste)) ==
               Result::Failed);
        assertCallSequence(h.script, {Call::Boundary, Call::Begin});
        assert(h.script.abortCount == 0U);
    }

    {
        Harness h;
        h.script.beginOutcome = BeginOutcome::Continued;
        {
            Transaction transaction(h.sequencer, h.history, Action::StepPaste);
            assert(transaction.openBoundary());
            assert(transaction.execute(execution(
                       h.script,
                       Plan::FullWithProspectiveGraph,
                       false,
                       2U,
                       1,
                       1,
                       Action::StepPaste
                   )) == Result::Failed);
        }
        assertCallSequence(
            h.script,
            {Call::Boundary, Call::Begin, Call::Abort}
        );
        assert(h.script.abortCount == 1U);
        assert(h.script.abortOwner == Owner::PageStructure);
        assert(h.script.abortKey == static_cast<uint8_t>(Action::StepPaste));
    }

    std::cout << "[PASS] Page begin accepts Started only and aborts Continued\n";
}

void test_failed_mutation_and_revalidation_abort_exactly_once() {
    {
        Harness h;
        h.script.mutationOutcome = MutationOutcome::Failed;
        {
            Transaction transaction(
                h.sequencer,
                h.history,
                Action::FocusedStepReset
            );
            assert(transaction.openBoundary());
            assert(transaction.execute(execution(
                       h.script,
                       Plan::FlatOnly,
                       false,
                       0U,
                       1,
                       1,
                       Action::FocusedStepReset)) ==
                   Result::Failed);
        }
        assertCallSequence(
            h.script,
            {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
             Call::Mutation, Call::Abort}
        );
        assert(h.script.abortCount == 1U);
    }

    {
        Harness h;
        h.script.revalidateOutcome = false;
        {
            Transaction transaction(
                h.sequencer,
                h.history,
                Action::StepSelectionReset
            );
            assert(transaction.openBoundary());
            assert(transaction.execute(execution(
                       h.script,
                       Plan::FullCurrentPayload,
                       false,
                       0U,
                       1,
                       1,
                       Action::StepSelectionReset)) ==
                   Result::Failed);
        }
        assertCallSequence(
            h.script,
            {Call::Boundary, Call::Begin, Call::Revalidate, Call::Abort}
        );
        assert(h.script.abortCount == 1U);
        assert(h.script.readyCount == 0U);
        assert(h.script.mutationCount == 0U);
    }

    std::cout << "[PASS] failed mutation/revalidation abort armed owner once\n";
}

void runSettlementCase(
    SealOutcome sealOutcome,
    CommitOutcome commitOutcome,
    Result expected,
    std::size_t expectedCommitCount,
    std::size_t expectedAbortCount,
    std::initializer_list<Call> expectedCalls
) {
    Harness h;
    h.script.sealOutcome = sealOutcome;
    h.script.commitOutcome = commitOutcome;
    h.script.mutationOutcome = MutationOutcome::Changed;
    {
        Transaction transaction(
            h.sequencer,
            h.history,
            Action::PageSelectionDeleteOrDeepReset
        );
        assert(transaction.openBoundary());
        assert(transaction.execute(execution(
                   h.script,
                   Plan::FullCurrentPayload,
                   true,
                   3U,
                   2,
                   4,
                   Action::PageSelectionDeleteOrDeepReset)) == expected);
    }

    assert(h.script.sealCount == 1U);
    assert(h.script.commitCount == expectedCommitCount);
    assert(h.script.finalizeCount ==
           (expected == Result::Committed ? 1U : 0U));
    assert(h.script.abortCount == expectedAbortCount);
    assert(h.script.mutationReceivedHistory);
    assertCallSequence(h.script, expectedCalls);
    assert(h.script.sealOwner == Owner::PageStructure);
    assert(h.script.sealKey ==
           static_cast<uint8_t>(
               Action::PageSelectionDeleteOrDeepReset
           ));
    if (expectedCommitCount != 0U) {
        assert(h.script.commitOwner == Owner::PageStructure);
    }
    if (expectedAbortCount != 0U) {
        assert(h.script.abortOwner == Owner::PageStructure);
        assert(h.script.abortKey ==
               static_cast<uint8_t>(
                   Action::PageSelectionDeleteOrDeepReset
               ));
    }
}

void test_seal_and_commit_outcomes_disarm_or_abort_exactly() {
    runSettlementCase(
        SealOutcome::Cleared,
        CommitOutcome::Failed,
        Result::NoChange,
        0U,
        0U,
        {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
         Call::Mutation, Call::Seal}
    );
    runSettlementCase(
        SealOutcome::FailedClosed,
        CommitOutcome::Committed,
        Result::Failed,
        0U,
        0U,
        {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
         Call::Mutation, Call::Seal}
    );
    runSettlementCase(
        SealOutcome::Failed,
        CommitOutcome::Committed,
        Result::Failed,
        0U,
        1U,
        {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
         Call::Mutation, Call::Seal, Call::Abort}
    );
    runSettlementCase(
        SealOutcome::Sealed,
        CommitOutcome::Committed,
        Result::Committed,
        1U,
        0U,
        {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
         Call::Mutation, Call::Seal, Call::Commit, Call::Finalize}
    );
    runSettlementCase(
        SealOutcome::Sealed,
        CommitOutcome::NoChange,
        Result::NoChange,
        1U,
        0U,
        {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
         Call::Mutation, Call::Seal, Call::Commit}
    );
    runSettlementCase(
        SealOutcome::Sealed,
        CommitOutcome::Failed,
        Result::Failed,
        1U,
        1U,
        {Call::Boundary, Call::Begin, Call::Revalidate, Call::Ready,
         Call::Mutation, Call::Seal, Call::Commit, Call::Abort}
    );

    std::cout << "[PASS] seal/commit outcomes disarm or abort exactly once\n";
}

void test_page_count_descriptor_is_frozen_across_seal() {
    Harness h;
    h.script.sealOutcome = SealOutcome::Cleared;
    h.script.mutationOutcome = MutationOutcome::NoChange;
    {
        Transaction transaction(
            h.sequencer,
            h.history,
            Action::PageSelectionReset
        );
        assert(transaction.openBoundary());
        assert(transaction.execute(execution(
                   h.script,
                   Plan::FlatOnly,
                   false,
                   15U,
                   4,
                   4,
                   Action::PageSelectionReset)) ==
               Result::NoChange);
    }

    assert(!h.script.beginDescriptor.hasValue);
    assert(h.script.beginDescriptor.beforeValue == 4);
    assert(h.script.beginDescriptor.afterValue == 4);
    assert(sameDescriptor(
        h.script.beginDescriptor,
        h.script.sealDescriptor
    ));

    std::cout << "[PASS] root Page-count descriptor is frozen across seal\n";
}

struct CoreHarness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    Services history;

    CoreHarness()
        : state(storages.settings),
          history(Services::fromCoreState(state)) {
        state.sequencer.pattern.setContentLength(8U);
        state.sequencer.pattern.note[0] = 60U;
        assert(seq::initializeTrackBankFromActive(
            state.sequencerTracks,
            state.sequencer
        ));
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        test_support::drainNotifications();
        state.flushProjectMutationCoalescing();
        state.acknowledgeProjectSessionSave(state.project.metadata.modifiedCounter);
    }
};

void authorCoreFullPayload(CoreHarness& h, bool nonemptyCc) {
    auto& pattern = h.state.sequencer.pattern;
    assert(seq::ensureGraphRoot(pattern));
    assert(seq::setNodeNoteOffset(
        pattern, seq::rootStepNodeId(0U), 5));
    auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
    assert(lanes != nullptr);
    if (nonemptyCc) {
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(seq::createSequencerCcLane(*lanes, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(*lanes, 0U, 0U, 99U).changed());
        pattern.bumpCcLaneRevision();
    }
    assert(seq::storeActiveTrack(
        h.state.sequencerTracks, h.state.sequencer));
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter);
}

void authorCoreDisabledGraphAndCc(CoreHarness& h) {
    auto& pattern = h.state.sequencer.pattern;
    pattern.graph = core::app::makeExtmemUnique<
        oc::note::sequencer::StepSequencerGraph>();
    assert(pattern.graph != nullptr);
    assert(seq::isCanonicalDisabledSequencerGraph(*pattern.graph));

    auto* lanes = seq::ensureSequencerCcLaneBank(pattern);
    assert(lanes != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74U;
    assert(seq::createSequencerCcLane(*lanes, 0U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*lanes, 0U, 0U, 99U).changed());
    pattern.bumpCcLaneRevision();

    assert(seq::storeActiveTrack(
        h.state.sequencerTracks, h.state.sequencer));
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter);

    const auto& bankPattern = h.state.sequencerTracks.track(0U);
    assert(bankPattern.graph == nullptr);
    assert(bankPattern.ccLanes != nullptr);
    assert(seq::sequencerCcLaneCount(*pattern.ccLanes) == 1U);
    assert(seq::sequencerCcLaneCount(*bankPattern.ccLanes) == 1U);
}

struct CoreMutation {
    uint8_t note = 60U;
    MutationOutcome outcome = MutationOutcome::Changed;
    bool installReplacementGraph = false;
    bool activateGraph = false;
    bool revalidateOutcome = true;
    std::size_t revalidateCount = 0U;
    std::size_t callCount = 0U;
    seq::SequencerHistoryGraphPtr replacementGraph;

    static bool revalidate(
        const void* context,
        const seq::SequencerState&
    ) noexcept {
        auto& self = *const_cast<CoreMutation*>(
            static_cast<const CoreMutation*>(context));
        ++self.revalidateCount;
        return self.revalidateOutcome;
    }

    static MutationOutcome apply(
        void* context,
        seq::SequencerState& sequencer,
        const Services&
    ) noexcept {
        auto& self = *static_cast<CoreMutation*>(context);
        ++self.callCount;
        if (self.installReplacementGraph) {
            if (!self.replacementGraph) return MutationOutcome::Failed;
            sequencer.pattern.graph = std::move(self.replacementGraph);
        } else if (self.activateGraph) {
            if (!seq::ensureGraphRoot(sequencer.pattern)) {
                return MutationOutcome::Failed;
            }
            if (!seq::setNodeNoteOffset(
                    sequencer.pattern,
                    seq::rootStepNodeId(0U),
                    7)) {
                return MutationOutcome::Failed;
            }
        } else if (self.outcome != MutationOutcome::NoChange) {
            (void)sequencer.setStepNoteAt(0U, self.note);
        }
        return self.outcome;
    }
};

Execution coreExecution(
    CoreMutation& mutation,
    Plan plan = Plan::FlatOnly,
    bool compactGraphOnSeal = false,
    uint8_t track = 0U,
    int32_t beforePages = 1,
    int32_t afterPages = 1,
    Action action = Action::PageClear
) {
    return {
        .payloadPlan = plan,
        .action = action,
        .expectedTrack = track,
        .beforePageCount = beforePages,
        .afterPageCount = afterPages,
        .compactGraphOnSeal = compactGraphOnSeal,
        .mutationContext = &mutation,
        .revalidate = &CoreMutation::revalidate,
        .mutate = &CoreMutation::apply,
    };
}

struct CoreLifecycleProbe {
    core::state::CoreState* state = nullptr;
    bool switchTrackBeforeReady = false;
    std::size_t beginCount = 0U;
    std::size_t readyCount = 0U;
    std::size_t sealCount = 0U;
    std::size_t commitCount = 0U;
    std::size_t abortCount = 0U;
    Owner lastOwner = Owner::PatternPitch;
    uint8_t lastKey = 0xFFU;
    SealOutcome lastSeal = SealOutcome::Failed;

    static BoundaryOutcome boundary(void* context) {
        auto& self = *static_cast<CoreLifecycleProbe*>(context);
        return self.state->commitSequencerPatternHistoryCoalescingOutcome();
    }

    static BeginOutcome begin(
        void* context,
        Owner owner,
        uint8_t key,
        Plan plan,
        seq::SequencerHistoryDescriptor descriptor,
        bool compactGraphOnSeal
    ) {
        auto& self = *static_cast<CoreLifecycleProbe*>(context);
        ++self.beginCount;
        self.lastOwner = owner;
        self.lastKey = key;
        return self.state->beginOrContinueSequencerPreparedPatternEdit(
            owner, key, plan, descriptor, compactGraphOnSeal);
    }

    static bool ready(
        void* context,
        Owner owner,
        uint8_t key,
        uint8_t expectedTrack
    ) {
        auto& self = *static_cast<CoreLifecycleProbe*>(context);
        ++self.readyCount;
        self.lastOwner = owner;
        self.lastKey = key;
        if (self.switchTrackBeforeReady) {
            self.switchTrackBeforeReady = false;
            assert(seq::switchActiveTrack(
                self.state->sequencerTracks,
                self.state->sequencer,
                1U
            ));
        }
        return self.state->sequencerPreparedPatternEditReady(
            owner, key, expectedTrack);
    }

    static SealOutcome seal(
        void* context,
        Owner owner,
        uint8_t key,
        bool mutationChanged,
        seq::SequencerHistoryDescriptor descriptor
    ) {
        auto& self = *static_cast<CoreLifecycleProbe*>(context);
        ++self.sealCount;
        self.lastOwner = owner;
        self.lastKey = key;
        self.lastSeal = self.state->sealSequencerPreparedPatternEdit(
            owner, key, mutationChanged, descriptor);
        return self.lastSeal;
    }

    static CommitOutcome commit(void* context, Owner owner) {
        auto& self = *static_cast<CoreLifecycleProbe*>(context);
        ++self.commitCount;
        self.lastOwner = owner;
        return self.state->commitSequencerPreparedPatternEdit(owner);
    }

    static AbortOutcome abort(
        void* context,
        Owner owner,
        uint8_t key
    ) {
        auto& self = *static_cast<CoreLifecycleProbe*>(context);
        ++self.abortCount;
        self.lastOwner = owner;
        self.lastKey = key;
        return self.state->abortSequencerPreparedPatternEdit(owner, key);
    }
};

constexpr Services::Operations kCoreLifecycleOperations{
    .commitCoalescedPatternEdit = &CoreLifecycleProbe::boundary,
    .beginPreparedPatternEdit = &CoreLifecycleProbe::begin,
    .preparedPatternEditReady = &CoreLifecycleProbe::ready,
    .sealPreparedPatternEdit = &CoreLifecycleProbe::seal,
    .commitPreparedPatternEdit = &CoreLifecycleProbe::commit,
    .abortPreparedPatternEdit = &CoreLifecycleProbe::abort,
};

Services probeServices(CoreLifecycleProbe& probe) {
    return Services::fromStaticOperations<kCoreLifecycleOperations>(&probe);
}

void prepareOlderSealedPatternEdit(CoreHarness& h) {
    constexpr auto owner = Owner::PatternEditor;
    constexpr uint8_t key = 91U;
    const auto descriptor = seq::SequencerHistoryDescriptor{
        .kind = seq::SequencerHistoryActionKind::StepEdit,
        .stepIndex = 0U,
    };
    assert(h.state.beginOrContinueSequencerPreparedPatternEdit(
               owner, key, Plan::FlatOnly, descriptor) == BeginOutcome::Started);
    assert(h.state.sequencer.setStepNoteAt(0U, 61U));
    assert(h.state.sealSequencerPreparedPatternEdit(
               owner, key, true, descriptor) == SealOutcome::Sealed);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
}

void test_core_boundary_and_page_commit_are_two_exact_transactions() {
    CoreHarness h;
    prepareOlderSealedPatternEdit(h);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == 0U);

    Transaction transaction(h.state.sequencer, h.history, Action::PageClear);
    assert(transaction.openBoundary());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.projectHistory.undoCount() == 1U);
    CoreMutation mutation{.note = 62U};
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        allocation_trace::Scope allocationTrace;
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FlatOnly,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageClear)) == Result::Committed);
        assertAllocationCount(1U);
        tx::assertMaxPlusOneStillArmed(1U);
    }
    tx::assertFailureInjectionReset();

    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 2U);
    assert(h.state.projectHistory.undoCount() == 2U);
    assert(h.state.sequencer.pattern.note[0] == 62U);
    assert(h.state.sequencerTracks.track(0U).note[0] == 62U);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[0] == 61U);
    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[0] == 60U);

    std::cout << "[PASS] prior boundary and Page commit publish separately once\n";
}

void test_core_action_mismatch_rejects_before_begin_and_allocation() {
    CoreHarness h;
    const auto before = tx::captureStateInvariant(h.state);
    CoreMutation mutation{.note = 99U};
    Transaction transaction(h.state.sequencer, h.history, Action::PageDelete);
    assert(transaction.openBoundary());

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        allocation_trace::Scope allocationTrace;
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FullCurrentPayload,
                   true,
                   0U,
                   1,
                   1,
                   Action::PageClear)) == Result::Failed);
        assertAllocationCount(0U);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    tx::assertFailureInjectionReset();

    assert(mutation.callCount == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    tx::assertStateInvariant(h.state, before);

    std::cout <<
        "[PASS] mismatched execution action rejects before begin/allocation\n";
}

void test_core_no_change_and_raii_abort_leave_no_page_owner() {
    CoreHarness h;
    const auto before = tx::captureStateInvariant(h.state);
    {
        CoreMutation mutation{.outcome = MutationOutcome::NoChange};
        Transaction noChange(h.state.sequencer, h.history, Action::PageDelete);
        assert(noChange.openBoundary());
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        assert(noChange.execute(coreExecution(
                   mutation,
                   Plan::FlatOnly,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageDelete)) == Result::NoChange);
        tx::assertMaxPlusOneStillArmed(1U);
    }
    tx::assertFailureInjectionReset();
    tx::assertStateInvariant(h.state, before);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    {
        CoreMutation mutation{
            .note = 70U,
            .outcome = MutationOutcome::Failed,
        };
        std::optional<Transaction> failedMutation;
        failedMutation.emplace(
            h.state.sequencer,
            h.history,
            Action::FocusedStepReset
        );
        assert(failedMutation->openBoundary());
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        assert(failedMutation->execute(coreExecution(
                   mutation,
                   Plan::FlatOnly,
                   false,
                   0U,
                   1,
                   1,
                   Action::FocusedStepReset)) == Result::Failed);
        failedMutation.reset();
        tx::assertMaxPlusOneStillArmed(1U);
    }
    tx::assertFailureInjectionReset();
    tx::assertStateInvariant(h.state, before);
    assert(h.state.sequencer.pattern.note[0] == 60U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout << "[PASS] Core no-change and failed mutation consume the Page owner\n";
}

void test_core_flat_rollback_preserves_disabled_graph_and_empty_cc_owners() {
    CoreHarness h;
    auto& pattern = h.state.sequencer.pattern;
    pattern.graph = core::app::makeExtmemUnique<
        oc::note::sequencer::StepSequencerGraph>();
    pattern.ccLanes = core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
    assert(pattern.graph && pattern.ccLanes);
    assert(seq::isCanonicalDisabledSequencerGraph(*pattern.graph));
    assert(seq::sequencerCcLaneCount(*pattern.ccLanes) == 0U);

    auto* const graphOwner = pattern.graph.get();
    auto* const ccOwner = pattern.ccLanes.get();
    const uint64_t graphHash = byteHash(graphOwner, sizeof(*graphOwner));
    const uint64_t ccHash = byteHash(ccOwner, sizeof(*ccOwner));
    const auto before = tx::captureStateInvariant(h.state);

    {
        CoreMutation mutation{.outcome = MutationOutcome::NoChange};
        Transaction transaction(
            h.state.sequencer,
            h.history,
            Action::PageClear
        );
        assert(transaction.openBoundary());
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        assert(transaction.execute(coreExecution(mutation)) == Result::NoChange);
        tx::assertMaxPlusOneStillArmed(1U);
    }
    tx::assertFailureInjectionReset();
    tx::assertStateInvariant(h.state, before);
    assert(pattern.graph.get() == graphOwner);
    assert(pattern.ccLanes.get() == ccOwner);
    assert(byteHash(graphOwner, sizeof(*graphOwner)) == graphHash);
    assert(byteHash(ccOwner, sizeof(*ccOwner)) == ccHash);

    {
        CoreMutation mutation{
            .note = 79U,
            .outcome = MutationOutcome::Failed,
        };
        Transaction transaction(
            h.state.sequencer,
            h.history,
            Action::PageClear
        );
        assert(transaction.openBoundary());
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FlatOnly,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageClear)) == Result::Failed);
        tx::assertMaxPlusOneStillArmed(1U);
    }
    tx::assertFailureInjectionReset();
    tx::assertStateInvariant(h.state, before);
    assert(pattern.note[0] == 60U);
    assert(pattern.graph.get() == graphOwner);
    assert(pattern.ccLanes.get() == ccOwner);
    assert(byteHash(graphOwner, sizeof(*graphOwner)) == graphHash);
    assert(byteHash(ccOwner, sizeof(*ccOwner)) == ccHash);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

    std::cout <<
        "[PASS] Flat rollback preserves disabled Graph and empty CC owners\n";
}

void test_core_disabled_graph_to_enabled_with_cc_lock_p_is_exact() {
    for (std::size_t ordinal = 1U;
         ordinal <= kDisabledGraphToEnabledWithCcRequests.size();
         ++ordinal) {
        CoreHarness h;
        authorCoreDisabledGraphAndCc(h);
        auto& editor = h.state.sequencer.pattern;
        auto& bank = h.state.sequencerTracks.track(0U);
        auto* const editorGraphOwner = editor.graph.get();
        auto* const editorCcOwner = editor.ccLanes.get();
        auto* const bankGraphOwner = bank.graph.get();
        auto* const bankCcOwner = bank.ccLanes.get();
        const uint64_t editorGraphHash =
            byteHash(editorGraphOwner, sizeof(*editorGraphOwner));
        const uint64_t editorCcHash =
            byteHash(editorCcOwner, sizeof(*editorCcOwner));
        const uint64_t bankCcHash =
            byteHash(bankCcOwner, sizeof(*bankCcOwner));
        const auto before = tx::captureStateInvariant(h.state);
        seq::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);

        CoreMutation mutation{.activateGraph = true};
        Transaction transaction(
            h.state.sequencer,
            h.history,
            Action::PagePaste
        );
        assert(transaction.openBoundary());
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            allocation_trace::Scope allocationTrace;
            assert(transaction.execute(coreExecution(
                       mutation,
                       Plan::FullWithProspectiveGraph,
                       false,
                       0U,
                       1,
                       1,
                       Action::PagePaste)) == Result::Failed);
            // The injected attempt returns before operator new; trace the
            // successful prefix and prove the intercepted ordinal separately.
            assertAllocationRequestPrefix(
                kDisabledGraphToEnabledWithCcRequests,
                ordinal - 1U
            );
            tx::assertFailureConsumed(ordinal);
        }
        tx::assertFailureInjectionReset();

        assert(mutation.callCount == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        tx::assertStateInvariant(h.state, before);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        assert(editor.graph.get() == editorGraphOwner);
        assert(editor.ccLanes.get() == editorCcOwner);
        assert(bank.graph.get() == bankGraphOwner);
        assert(bank.ccLanes.get() == bankCcOwner);
        assert(byteHash(editorGraphOwner, sizeof(*editorGraphOwner)) ==
               editorGraphHash);
        assert(byteHash(editorCcOwner, sizeof(*editorCcOwner)) == editorCcHash);
        assert(byteHash(bankCcOwner, sizeof(*bankCcOwner)) == bankCcHash);
        assert(seq::isCanonicalDisabledSequencerGraph(*editorGraphOwner));
        assert(bankGraphOwner == nullptr);
    }

    CoreHarness h;
    authorCoreDisabledGraphAndCc(h);
    const auto before = tx::captureStateInvariant(h.state);
    CoreMutation mutation{.activateGraph = true};
    Transaction transaction(
        h.state.sequencer,
        h.history,
        Action::PagePaste
    );
    assert(transaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            kDisabledGraphToEnabledWithCcRequests.size() + 1U
        );
        allocation_trace::Scope allocationTrace;
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FullWithProspectiveGraph,
                   false,
                   0U,
                   1,
                   1,
                   Action::PagePaste)) == Result::Committed);
        assertAllocationRequestPrefix(
            kDisabledGraphToEnabledWithCcRequests,
            kDisabledGraphToEnabledWithCcRequests.size()
        );
        tx::assertMaxPlusOneStillArmed(
            kDisabledGraphToEnabledWithCcRequests.size()
        );
    }
    tx::assertFailureInjectionReset();

    const auto after = tx::captureStateInvariant(h.state);
    assert(mutation.callCount == 1U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(seq::graphView(h.state.sequencer.pattern) != nullptr);
    assert(seq::graphView(h.state.sequencerTracks.track(0U)) != nullptr);
    assert(after.editorGraphOwner == before.editorGraphOwner);
    assert(after.editorCcOwner == before.editorCcOwner);
    assert(after.bankGraphOwner != nullptr);
    assert(after.bankGraphOwner != before.bankGraphOwner);
    assert(after.bankGraphOwner != after.editorGraphOwner);
    assert(after.bankCcOwner != nullptr);
    assert(after.bankCcOwner != before.bankCcOwner);
    assert(after.bankCcOwner != after.editorCcOwner);
    assert(after.sequencerUndoCount == before.sequencerUndoCount + 1U);
    assert(after.projectUndoCount == before.projectUndoCount + 1U);
    assert(after.sequencerUndoIdentity != 0U);
    assert(after.sequencerUndoIdentity != before.sequencerUndoIdentity);
    assert(after.modifiedCounter == before.modifiedCounter + 1U);
    assert(after.dirty);
    assert(after.sessionSavePending);
    assert(byteHash(
               h.state.sequencer.pattern.graph.get(),
               sizeof(*h.state.sequencer.pattern.graph)) ==
           byteHash(
               h.state.sequencerTracks.track(0U).graph.get(),
               sizeof(*h.state.sequencerTracks.track(0U).graph)));
    assert(byteHash(
               h.state.sequencer.pattern.ccLanes.get(),
               sizeof(*h.state.sequencer.pattern.ccLanes)) ==
           byteHash(
               h.state.sequencerTracks.track(0U).ccLanes.get(),
               sizeof(*h.state.sequencerTracks.track(0U).ccLanes)));

    std::cout <<
        "[PASS] disabled Graph to enabled plus CC LOCK-P is exact at 1..6 and max+1\n";
}

void commitMaximalPageHistoryEntry(CoreHarness& h, uint8_t note) {
    CoreMutation mutation{.note = note};
    Transaction transaction(
        h.state.sequencer,
        h.history,
        Action::PageClear
    );
    assert(transaction.openBoundary());
    assert(transaction.execute(coreExecution(
               mutation,
               Plan::FullCurrentPayload,
               false,
               0U,
               1,
               1,
               Action::PageClear)) == Result::Committed);
    assert(mutation.callCount == 1U);
    assert(h.state.sequencer.pattern.note[0U] == note);
    assert(h.state.sequencerTracks.track(0U).note[0U] == note);
    test_support::drainNotifications();
}

void test_core_near_budget_page_reservation_is_pre_live_and_prunes_exactly() {
    CoreHarness h;
    authorCoreFullPayload(h, true);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.sequencerHistory.retainedBytes() == 0U);

    constexpr uint8_t firstCommittedNote = 61U;
    uint8_t nextNote = firstCommittedNote;
    commitMaximalPageHistoryEntry(h, nextNote++);
    const std::size_t entryBytes = h.state.sequencerHistory.retainedBytes();
    const std::size_t expectedEntryBytes =
        sizeof(seq::SequencerHistoryPatternChange) +
        2U * sizeof(oc::note::sequencer::StepSequencerGraph) +
        2U * sizeof(seq::SequencerCcLaneBank) +
        5U * kArmAllocationHeaderBytes;
    assert(entryBytes == expectedEntryBytes);

    std::size_t committedCount = 1U;
    while (h.state.sequencerHistory.retainedBytes() + entryBytes <=
           seq::SequencerHistoryService::RETAINED_BYTE_BUDGET) {
        assert(committedCount <
               seq::SequencerHistoryService::PATTERN_ENTRY_LIMIT);
        commitMaximalPageHistoryEntry(h, nextNote++);
        ++committedCount;
        assert(h.state.sequencerHistory.undoCount() == committedCount);
        assert(h.state.sequencerHistory.retainedBytes() ==
               committedCount * entryBytes);
    }

    assert(committedCount ==
           seq::SequencerHistoryService::PATTERN_ENTRY_LIMIT - 1U);
    assert(h.state.sequencerHistory.retainedBytes() + entryBytes >
           seq::SequencerHistoryService::RETAINED_BYTE_BUDGET);

    // The existing policy admits one incoming entry by its own retained size,
    // then prunes the oldest retained entries during the no-allocation
    // ownership transfer. Failure of the final reserve must therefore happen
    // before the mutation and preserve the already-near-budget history exactly.
    const auto beforeRejectedAdmission = tx::captureStateInvariant(h.state);
    seq::SequencerHistoryPatternSnapshot musicalBeforeRejectedAdmission;
    tx::captureMusicalSnapshot(h.state, musicalBeforeRejectedAdmission);
    CoreMutation rejectedMutation{.note = nextNote};
    Transaction rejectedTransaction(
        h.state.sequencer,
        h.history,
        Action::PageClear
    );
    assert(rejectedTransaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            kEnabledGraphWithCcRequests.size()
        );
        allocation_trace::Scope allocationTrace;
        assert(rejectedTransaction.execute(coreExecution(
                   rejectedMutation,
                   Plan::FullCurrentPayload,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageClear)) == Result::Failed);
        // The seventh attempt is intercepted before operator new.
        assertAllocationRequestPrefix(
            kEnabledGraphWithCcRequests,
            kEnabledGraphWithCcRequests.size() - 1U
        );
        tx::assertFailureConsumed(kEnabledGraphWithCcRequests.size());
    }
    tx::assertFailureInjectionReset();
    assert(rejectedMutation.callCount == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    tx::assertStateInvariant(h.state, beforeRejectedAdmission);
    tx::assertMusicalSnapshot(h.state, musicalBeforeRejectedAdmission);

    // With every reserve present, max+1 stays armed through the live write,
    // admission, pruning and publication. The scope count is still below its
    // own limit, so the one-for-one eviction below is caused by bytes alone.
    const auto beforePrune = tx::captureStateInvariant(h.state);
    CoreMutation admittedMutation{.note = nextNote};
    Transaction admittedTransaction(
        h.state.sequencer,
        h.history,
        Action::PageClear
    );
    assert(admittedTransaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            kEnabledGraphWithCcRequests.size() + 1U
        );
        allocation_trace::Scope allocationTrace;
        assert(admittedTransaction.execute(coreExecution(
                   admittedMutation,
                   Plan::FullCurrentPayload,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageClear)) == Result::Committed);
        assertAllocationRequestPrefix(
            kEnabledGraphWithCcRequests,
            kEnabledGraphWithCcRequests.size()
        );
        tx::assertMaxPlusOneStillArmed(
            kEnabledGraphWithCcRequests.size()
        );
    }
    tx::assertFailureInjectionReset();

    const auto afterPrune = tx::captureStateInvariant(h.state);
    assert(admittedMutation.callCount == 1U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(afterPrune.sequencerUndoCount == beforePrune.sequencerUndoCount);
    assert(afterPrune.projectUndoCount == beforePrune.projectUndoCount);
    assert(afterPrune.retainedBytes == beforePrune.retainedBytes);
    assert(afterPrune.retainedBytes <=
           seq::SequencerHistoryService::RETAINED_BYTE_BUDGET);
    assert(afterPrune.sequencerUndoIdentity !=
           beforePrune.sequencerUndoIdentity);
    assert(afterPrune.modifiedCounter == beforePrune.modifiedCounter + 1U);
    assert(afterPrune.dirty);
    assert(afterPrune.sessionSavePending);

    const uint8_t retainedUndoCount = afterPrune.sequencerUndoCount;
    for (uint8_t index = 0U; index < retainedUndoCount; ++index) {
        assert(h.state.undoSequencerHistory());
    }
    assert(!h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[0U] == firstCommittedNote);
    assert(h.state.sequencerTracks.track(0U).note[0U] == firstCommittedNote);

    std::cout <<
        "[PASS] near-budget Page reservation is pre-live and byte pruning is exact\n";
}

void test_core_track_drift_revalidation_restores_inactive_owner() {
    CoreHarness h;
    authorCoreFullPayload(h, true);
    auto* const graphOwner = h.state.sequencer.pattern.graph.get();
    auto* const ccOwner = h.state.sequencer.pattern.ccLanes.get();
    const uint32_t graphRevision = h.state.sequencer.pattern.graphRevision.get();
    const uint32_t ccRevision = h.state.sequencer.pattern.ccLaneRevision.get();
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    h.state.sequencerTracks.track(1U).note[0] = 41U;
    CoreLifecycleProbe probe{
        .state = &h.state,
        .switchTrackBeforeReady = true,
    };
    auto history = probeServices(probe);
    CoreMutation mutation{.note = 72U};

    Transaction transaction(h.state.sequencer, history, Action::PageClear);
    assert(transaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        allocation_trace::Scope allocationTrace;
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FlatOnly,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageClear)) == Result::Failed);
        assertAllocationCount(1U);
        tx::assertMaxPlusOneStillArmed(1U);
    }
    tx::assertFailureInjectionReset();

    assert(probe.beginCount == 1U);
    assert(probe.readyCount == 1U);
    assert(probe.sealCount == 0U);
    assert(probe.commitCount == 0U);
    assert(probe.abortCount == 1U);
    assert(probe.lastOwner == Owner::PageStructure);
    assert(probe.lastKey == static_cast<uint8_t>(Action::PageClear));
    assert(mutation.callCount == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(h.state.sequencerTracks.track(0U).note[0] == 60U);
    assert(h.state.sequencerTracks.track(0U).graph.get() == graphOwner);
    assert(h.state.sequencerTracks.track(0U).ccLanes.get() == ccOwner);
    assert(h.state.sequencerTracks.track(0U).graphRevision.get() == graphRevision);
    assert(h.state.sequencerTracks.track(0U).ccLaneRevision.get() == ccRevision);
    assert(seq::graphView(h.state.sequencerTracks.track(0U)) != nullptr);
    assert(seq::sequencerCcLaneCount(
               *h.state.sequencerTracks.track(0U).ccLanes) == 1U);
    assert(h.state.sequencer.pattern.note[0] == 41U);
    assert(h.state.abortSequencerPreparedPatternEdit(
               Owner::PageStructure,
               static_cast<uint8_t>(Action::PageClear)) == AbortOutcome::NoPending);

    std::cout << "[PASS] Track drift rejects before write and restores inactive owner\n";
}

void test_core_track_drift_preserves_full_payload_owner_identity() {
    CoreHarness h;
    authorCoreFullPayload(h, false);
    auto* const graphOwner = h.state.sequencer.pattern.graph.get();
    auto* const ccOwner = h.state.sequencer.pattern.ccLanes.get();
    assert(graphOwner != nullptr);
    assert(ccOwner != nullptr);
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    h.state.sequencerTracks.track(1U).note[0] = 42U;

    CoreLifecycleProbe probe{
        .state = &h.state,
        .switchTrackBeforeReady = true,
    };
    auto history = probeServices(probe);
    CoreMutation mutation{.note = 73U};

    Transaction transaction(h.state.sequencer, history, Action::PageDelete);
    assert(transaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        allocation_trace::Scope allocationTrace;
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FullCurrentPayload,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageDelete)) == Result::Failed);
        assertAllocationCount(4U);
        tx::assertMaxPlusOneStillArmed(4U);
    }
    tx::assertFailureInjectionReset();

    assert(probe.beginCount == 1U);
    assert(probe.readyCount == 1U);
    assert(probe.sealCount == 0U);
    assert(probe.abortCount == 1U);
    assert(mutation.callCount == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(h.state.sequencer.pattern.note[0] == 42U);
    assert(h.state.sequencerTracks.track(0U).note[0] == 60U);
    assert(h.state.sequencerTracks.track(0U).graph.get() == graphOwner);
    assert(h.state.sequencerTracks.track(0U).ccLanes.get() == ccOwner);
    assert(seq::graphView(h.state.sequencerTracks.track(0U)) != nullptr);
    assert(seq::sequencerCcLaneCount(
               *h.state.sequencerTracks.track(0U).ccLanes) == 0U);

    std::cout << "[PASS] inactive Full Graph/CC rollback preserves owner identity\n";
}

void test_core_track_drift_removes_prospective_graph_exactly() {
    CoreHarness h;
    h.state.sequencerTracks.syncSharedTrackState(0x0003U, 0U);
    h.state.sequencerTracks.track(1U).note[0] = 43U;
    CoreLifecycleProbe probe{
        .state = &h.state,
        .switchTrackBeforeReady = true,
    };
    auto history = probeServices(probe);
    CoreMutation mutation{.note = 74U};

    Transaction transaction(
        h.state.sequencer,
        history,
        Action::PageSelectionPaste
    );
    assert(transaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        allocation_trace::Scope allocationTrace;
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FullWithProspectiveGraph,
                   false,
                   0U,
                   1,
                   1,
                   Action::PageSelectionPaste)) == Result::Failed);
        assertAllocationCount(4U);
        tx::assertMaxPlusOneStillArmed(4U);
    }
    tx::assertFailureInjectionReset();

    assert(probe.beginCount == 1U);
    assert(probe.readyCount == 1U);
    assert(probe.sealCount == 0U);
    assert(probe.abortCount == 1U);
    assert(mutation.callCount == 0U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1U);
    assert(h.state.sequencerTracks.track(0U).note[0] == 60U);
    assert(h.state.sequencerTracks.track(0U).graph == nullptr);
    assert(h.state.sequencerTracks.track(0U).ccLanes == nullptr);
    assert(h.state.sequencer.pattern.note[0] == 43U);

    std::cout << "[PASS] Track drift removes a prospective Graph exactly\n";
}

void test_core_released_prospective_graph_can_fail_closed() {
    CoreHarness h;
    const auto before = tx::captureStateInvariant(h.state);
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);
    CoreLifecycleProbe probe{.state = &h.state};
    auto history = probeServices(probe);
    CoreMutation mutation{.note = 75U};

    Transaction transaction(
        h.state.sequencer,
        history,
        Action::PageSelectionDeleteOrDeepReset
    );
    assert(transaction.openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(5U);
        allocation_trace::Scope allocationTrace;
        assert(transaction.execute(coreExecution(
                   mutation,
                   Plan::FullWithProspectiveGraph,
                   true,
                   0U,
                   1,
                   1,
                   Action::PageSelectionDeleteOrDeepReset)) == Result::Failed);
        assertAllocationCount(4U);
        tx::assertMaxPlusOneStillArmed(4U);
    }
    tx::assertFailureInjectionReset();

    assert(probe.beginCount == 1U);
    assert(probe.readyCount == 1U);
    assert(probe.sealCount == 1U);
    assert(probe.lastSeal == SealOutcome::FailedClosed);
    assert(probe.commitCount == 0U);
    assert(probe.abortCount == 0U);
    assert(mutation.callCount == 1U);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.graph == nullptr);
    tx::assertMusicalSnapshot(h.state, musicalBefore);
    tx::assertStateInvariant(h.state, before);

    std::cout << "[PASS] released prospective Graph preserves FailedClosed rollback\n";
}

void test_core_real_failed_closed_disarms_without_second_abort() {
    CoreHarness h;
    const auto before = tx::captureStateInvariant(h.state);
    seq::SequencerHistoryPatternSnapshot musicalBefore;
    tx::captureMusicalSnapshot(h.state, musicalBefore);

    CoreLifecycleProbe probe{.state = &h.state};
    auto history = probeServices(probe);
    CoreMutation mutation{
        .installReplacementGraph = true,
        .replacementGraph = core::app::makeExtmemUnique<
            oc::note::sequencer::StepSequencerGraph>(),
    };
    assert(mutation.replacementGraph);

    std::optional<Transaction> transaction;
    transaction.emplace(h.state.sequencer, history, Action::PagePaste);
    assert(transaction->openBoundary());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        allocation_trace::Scope allocationTrace;
        assert(transaction->execute(coreExecution(
                   mutation,
                   Plan::FlatOnly,
                   false,
                   0U,
                   1,
                   1,
                   Action::PagePaste)) == Result::Failed);
        assertAllocationCount(1U);
        assert(probe.lastSeal == SealOutcome::FailedClosed);
        assert(probe.sealCount == 1U);
        assert(probe.commitCount == 0U);
        assert(probe.abortCount == 0U);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        transaction.reset();
        assert(probe.abortCount == 0U);
        assert(h.state.abortSequencerPreparedPatternEdit(
                   Owner::PageStructure,
                   static_cast<uint8_t>(Action::PagePaste)) ==
               AbortOutcome::NoPending);
        tx::assertMaxPlusOneStillArmed(1U);
    }
    tx::assertFailureInjectionReset();

    assert(probe.beginCount == 1U);
    assert(probe.readyCount == 1U);
    assert(probe.lastOwner == Owner::PageStructure);
    assert(probe.lastKey == static_cast<uint8_t>(Action::PagePaste));
    assert(mutation.callCount == 1U);
    tx::assertMusicalSnapshot(h.state, musicalBefore);
    tx::assertStateInvariant(h.state, before);

    std::cout << "[PASS] real Core FailedClosed consumes owner without double abort\n";
}

int runFatalInvariantCase(const char* name) {
    Harness h;
    Transaction transaction(h.sequencer, h.history, Action::PageDelete);
    assert(transaction.openBoundary());

    if (std::strcmp(name, "--fatal-abort-not-aborted") == 0) {
        h.script.revalidateOutcome = false;
        h.script.abortOutcome = AbortOutcome::Failed;
    } else if (std::strcmp(name, "--fatal-commit-no-pending") == 0) {
        h.script.commitOutcome = CommitOutcome::NoPending;
    } else if (std::strcmp(name, "--fatal-invalid-seal") == 0) {
        h.script.sealOutcome = static_cast<SealOutcome>(0xFFU);
    } else {
        return 2;
    }

    (void)transaction.execute(execution(
        h.script,
        Plan::FlatOnly,
        false,
        0U,
        1,
        1,
        Action::PageDelete));
    return 0;
}

bool childTerminatesAbnormally(const char* name) {
    assert(executablePath != nullptr);
#if defined(_WIN32)
    const intptr_t status = _spawnl(
        _P_WAIT,
        executablePath,
        executablePath,
        name,
        static_cast<char*>(nullptr)
    );
    assert(status != -1);
    return status != 0;
#else
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl(executablePath, executablePath, name, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    return WIFSIGNALED(status) ||
           (WIFEXITED(status) && WEXITSTATUS(status) != 0);
#endif
}

void test_armed_invariant_failures_are_release_fatal() {
    assert(childTerminatesAbnormally("--fatal-abort-not-aborted"));
    assert(childTerminatesAbnormally("--fatal-commit-no-pending"));
    assert(childTerminatesAbnormally("--fatal-invalid-seal"));

    std::cout << "[PASS] armed abort, missing commit and invalid seal fail-stop\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) return runFatalInvariantCase(argv[1]);
    executablePath = argv[0];

    test_exact_nine_actions_forward_stable_owner_keys();
    test_draft_rejects_before_the_only_boundary();
    test_boundary_failure_and_one_shot_protocol_stop_before_begin();
    test_begin_accepts_started_only_and_closes_continued();
    test_failed_mutation_and_revalidation_abort_exactly_once();
    test_seal_and_commit_outcomes_disarm_or_abort_exactly();
    test_page_count_descriptor_is_frozen_across_seal();
    test_core_boundary_and_page_commit_are_two_exact_transactions();
    test_core_action_mismatch_rejects_before_begin_and_allocation();
    test_core_no_change_and_raii_abort_leave_no_page_owner();
    test_core_flat_rollback_preserves_disabled_graph_and_empty_cc_owners();
    test_core_disabled_graph_to_enabled_with_cc_lock_p_is_exact();
    test_core_near_budget_page_reservation_is_pre_live_and_prunes_exactly();
    test_core_track_drift_revalidation_restores_inactive_owner();
    test_core_track_drift_preserves_full_payload_owner_identity();
    test_core_track_drift_removes_prospective_graph_exactly();
    test_core_released_prospective_graph_can_fail_closed();
    test_core_real_failed_closed_disarms_without_second_abort();
    test_armed_invariant_failures_are_release_fatal();
    std::cout <<
        "All SequencerPreparedPageStructureTransaction tests passed.\n";
    return 0;
}
