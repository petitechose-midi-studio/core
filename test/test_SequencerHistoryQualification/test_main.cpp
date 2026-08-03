#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "support/CoreStorages.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace {

namespace seq = core::state::sequencer;
namespace tx = test_support::sequencer_transaction;

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;

    Harness()
        : state(storages.settings) {}
};

enum class PayloadKind : uint8_t {
    FlatOnly,
    GraphOnly,
    CcOnly,
    GraphAndCc,
};

constexpr bool hasGraph(PayloadKind kind) {
    return kind == PayloadKind::GraphOnly || kind == PayloadKind::GraphAndCc;
}

constexpr bool hasCc(PayloadKind kind) {
    return kind == PayloadKind::CcOnly || kind == PayloadKind::GraphAndCc;
}

void preparePayload(Harness& h, PayloadKind kind) {
    auto& pattern = h.state.sequencer.pattern;
    pattern.setContentLength(8U);
    assert(pattern.setStepDataAt(
        0U,
        60U,
        91U,
        seq::SequencerPatternState::DEFAULT_GATE_PERCENT
    ));
    pattern.setEnabled(0U, true);

    if (hasGraph(kind)) {
        assert(seq::ensureGraphRoot(pattern));
        assert(seq::setNodeNoteOffset(
            pattern,
            seq::rootStepNodeId(0U),
            5
        ));
    }

    if (hasCc(kind)) {
        auto* bank = seq::ensureSequencerCcLaneBank(pattern);
        assert(bank != nullptr);
        seq::SequencerCcLaneDraft draft{};
        draft.destination.controller = 74U;
        assert(seq::createSequencerCcLane(*bank, 0U, draft).changed());
        assert(seq::setSequencerCcLaneEvent(*bank, 0U, 0U, 99U).changed());
        pattern.bumpCcLaneRevision();
    }
}

struct TrivialProbe {
    uint32_t value;
};

void test_all_extmem_helpers_share_the_fail_nth_seam() {
    const TrivialProbe source{0xC0DEF00DU};

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        auto value = core::app::makeExtmemUnique<TrivialProbe>();
        assert(!value);
        tx::assertFailureConsumed(1U);
    }
    tx::assertFailureInjectionReset();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        auto value = core::app::makeExtmemUnique<TrivialProbe>();
        assert(value);
        value->value = source.value;
        tx::assertMaxPlusOneStillArmed(1U);
        auto rejected = core::app::makeExtmemUnique<TrivialProbe>();
        assert(!rejected);
        tx::assertFailureConsumed(2U);
    }
    tx::assertFailureInjectionReset();

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        auto value = core::app::makeExtmemUniqueCopy(source);
        assert(!value);
        tx::assertFailureConsumed(1U);
    }
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        auto value = core::app::makeExtmemUniqueCopy(source);
        assert(value && value->value == source.value);
        tx::assertMaxPlusOneStillArmed(1U);
        auto rejected = core::app::makeExtmemUniqueCopy(source);
        assert(!rejected);
        tx::assertFailureConsumed(2U);
    }

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        auto value = core::app::makeExtmemUniqueForOverwrite<TrivialProbe>();
        assert(!value);
        tx::assertFailureConsumed(1U);
    }
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        auto value = core::app::makeExtmemUniqueForOverwrite<TrivialProbe>();
        assert(value);
        value->value = source.value;
        assert(value->value == source.value);
        tx::assertMaxPlusOneStillArmed(1U);
        auto rejected =
            core::app::makeExtmemUniqueForOverwrite<TrivialProbe>();
        assert(!rejected);
        tx::assertFailureConsumed(2U);
    }

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        auto values =
            core::app::makeExtmemUniqueArrayForOverwrite<TrivialProbe>(2U);
        assert(!values);
        tx::assertFailureConsumed(1U);
    }
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        auto values =
            core::app::makeExtmemUniqueArrayForOverwrite<TrivialProbe>(2U);
        assert(values);
        values[0].value = source.value;
        values[1].value = source.value + 1U;
        assert(values[0].value == source.value);
        assert(values[1].value == source.value + 1U);
        tx::assertMaxPlusOneStillArmed(1U);
        auto rejected =
            core::app::makeExtmemUniqueArrayForOverwrite<TrivialProbe>(2U);
        assert(!rejected);
        tx::assertFailureConsumed(2U);
    }

    {
        constexpr std::size_t tooLarge =
            std::numeric_limits<std::size_t>::max() / sizeof(TrivialProbe) + 1U;
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        auto empty =
            core::app::makeExtmemUniqueArrayForOverwrite<TrivialProbe>(0U);
        auto overflow =
            core::app::makeExtmemUniqueArrayForOverwrite<TrivialProbe>(tooLarge);
        assert(!empty && !overflow);
        tx::assertMaxPlusOneStillArmed(0U);
    }
    tx::assertFailureInjectionReset();

    std::cout << "[PASS] all EXTMEM helpers share the fail-Nth seam\n";
}

void verifySnapshotFailure(
    PayloadKind kind,
    std::size_t ordinal
) {
    Harness h;
    preparePayload(h, kind);
    seq::SequencerHistoryPatternSnapshot musicalBaseline;
    tx::captureMusicalSnapshot(h.state, musicalBaseline);
    const auto invariant = tx::captureStateInvariant(h.state);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        seq::SequencerHistoryPatternSnapshot rejected;
        assert(!seq::captureHistorySnapshot(h.state.sequencer, rejected));
        tx::assertFailureConsumed(ordinal);
        tx::assertStateInvariant(h.state, invariant);
    }

    tx::assertMusicalSnapshot(h.state, musicalBaseline);
}

void verifySnapshotAllocationRatchet(
    PayloadKind kind,
    std::size_t expectedAttempts
) {
    Harness h;
    preparePayload(h, kind);
    seq::SequencerHistoryPatternSnapshot musicalBaseline;
    tx::captureMusicalSnapshot(h.state, musicalBaseline);
    const auto invariant = tx::captureStateInvariant(h.state);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        seq::SequencerHistoryPatternSnapshot captured;
        assert(seq::captureHistorySnapshot(h.state.sequencer, captured));
        assert(static_cast<bool>(captured.graph) == hasGraph(kind));
        assert(static_cast<bool>(captured.ccLanes) == hasCc(kind));
        assert(captured.ccLanesCaptured);
        tx::assertMaxPlusOneStillArmed(expectedAttempts);
        tx::assertStateInvariant(h.state, invariant);
    }

    tx::assertMusicalSnapshot(h.state, musicalBaseline);
}

void test_snapshot_capture_allocation_matrix_is_atomic() {
    struct Case {
        PayloadKind kind;
        std::size_t expectedAttempts;
    };
    constexpr std::array<Case, 4> cases{{
        {PayloadKind::FlatOnly, 0U},
        {PayloadKind::GraphOnly, 1U},
        {PayloadKind::CcOnly, 1U},
        {PayloadKind::GraphAndCc, 2U},
    }};

    for (const auto& item : cases) {
        for (std::size_t ordinal = 1U;
             ordinal <= item.expectedAttempts;
             ++ordinal) {
            verifySnapshotFailure(item.kind, ordinal);
        }
        verifySnapshotAllocationRatchet(item.kind, item.expectedAttempts);
    }

    std::cout << "[PASS] snapshot capture allocation matrix is atomic\n";
}

void prepareFlatHistoryPair(
    Harness& h,
    seq::SequencerHistoryPatternSnapshot& before,
    seq::SequencerHistoryPatternSnapshot& after
) {
    seq::captureFlatHistorySnapshot(h.state.sequencer, before);
    assert(!h.state.sequencer.pattern.isEnabled(1U));
    h.state.sequencer.pattern.setEnabled(1U, true);
    seq::captureFlatHistorySnapshot(h.state.sequencer, after);
}

void test_admitted_flat_pattern_has_one_atomic_change_allocation() {
    {
        Harness h;
        seq::SequencerHistoryPatternSnapshot before;
        seq::SequencerHistoryPatternSnapshot after;
        prepareFlatHistoryPair(h, before, after);
        seq::SequencerHistoryPatternSnapshot musicalBaseline;
        tx::captureMusicalSnapshot(h.state, musicalBaseline);
        const auto invariant = tx::captureStateInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(!tx::commitAdmittedPattern(
                h.state.sequencerHistory,
                std::move(before),
                std::move(after),
                seq::SequencerHistoryDescriptor{},
                seq::SequencerHistoryPatternStorage::FlatOnly
            ));
            tx::assertFailureConsumed(1U);
            tx::assertStateInvariant(h.state, invariant);
        }

        tx::assertMusicalSnapshot(h.state, musicalBaseline);
    }

    {
        Harness h;
        seq::SequencerHistoryPatternSnapshot before;
        seq::SequencerHistoryPatternSnapshot after;
        prepareFlatHistoryPair(h, before, after);
        seq::SequencerHistoryPatternSnapshot musicalBaseline;
        tx::captureMusicalSnapshot(h.state, musicalBaseline);
        const auto invariant = tx::captureStateInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(2U);
            assert(tx::commitAdmittedPattern(
                h.state.sequencerHistory,
                std::move(before),
                std::move(after),
                seq::SequencerHistoryDescriptor{},
                seq::SequencerHistoryPatternStorage::FlatOnly
            ));
            tx::assertMaxPlusOneStillArmed(1U);
            tx::assertSingleCommittedPublication(h.state, invariant, false);
        }

        tx::assertMusicalSnapshot(h.state, musicalBaseline);
    }

    std::cout << "[PASS] admitted FlatOnly commit has one atomic Change allocation\n";
}

void test_prepared_pattern_publication_is_allocation_free() {
    Harness h;
    preparePayload(h, PayloadKind::GraphAndCc);

    seq::SequencerHistoryPatternSnapshot before;
    assert(seq::captureHistorySnapshot(h.state.sequencer, before));
    assert(!h.state.sequencer.pattern.isEnabled(1U));
    h.state.sequencer.pattern.setEnabled(1U, true);
    seq::SequencerHistoryPatternSnapshot after;
    assert(seq::captureHistorySnapshot(h.state.sequencer, after));

    auto change = core::app::makeExtmemUnique<seq::SequencerHistoryPatternChange>();
    assert(change);
    change->trackIndex = h.state.sequencerTracks.activeTrackIndex();
    change->storage = seq::SequencerHistoryPatternStorage::FullGraph;
    change->descriptor.kind = seq::SequencerHistoryActionKind::StepToggle;
    change->descriptor.trackIndex = change->trackIndex;
    change->descriptor.stepIndex = 1U;
    change->before = std::move(before);
    change->after = std::move(after);

    auto history = core::handler::SequencerHistoryDomainServices::fromCoreState(
        h.state
    );
    assert(history.canRecordPattern(*change));
    seq::SequencerHistoryPatternSnapshot musicalBaseline;
    tx::captureMusicalSnapshot(h.state, musicalBaseline);
    const auto invariant = tx::captureStateInvariant(h.state);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        history.recordPreparedPattern(std::move(change));
        tx::assertMaxPlusOneStillArmed(0U);
        tx::assertSingleCommittedPublication(h.state, invariant, true);
    }

    tx::assertMusicalSnapshot(h.state, musicalBaseline);

    std::cout << "[PASS] prepared Pattern publication is allocation-free\n";
}

}  // namespace

int main() {
    test_all_extmem_helpers_share_the_fail_nth_seam();
    test_snapshot_capture_allocation_matrix_is_atomic();
    test_admitted_flat_pattern_has_one_atomic_change_allocation();
    test_prepared_pattern_publication_is_allocation_free();
    std::cout << "Sequencer History qualification tests passed\n";
    return 0;
}
