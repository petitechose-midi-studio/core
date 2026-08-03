#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>

#include <config/InputIDs.hpp>
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerPatternQuickControlsHandler.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerQuickControlsDraft.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "support/CoreStorages.hpp"
#include "support/InputTestHardware.hpp"
#include "support/NotificationTestUtils.hpp"
#include "support/SequencerHistoryTransactionAssertions.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace allocation_trace {

constexpr std::size_t MAX_REQUESTS = 12U;
bool enabled = false;
std::array<std::size_t, MAX_REQUESTS> requests{};
std::size_t count = 0U;
bool overflow = false;

void record(std::size_t bytes) {
    if (!enabled) return;
    const bool transactionRequest =
        bytes == sizeof(core::state::sequencer::SequencerQuickControlsDraft) ||
        bytes == sizeof(core::state::sequencer::SequencerHistoryPatternChange) ||
        bytes == sizeof(oc::note::sequencer::StepSequencerGraph) ||
        bytes == sizeof(core::state::sequencer::SequencerCcLaneBank);
    if (!transactionRequest) return;
    if (count >= requests.size()) {
        overflow = true;
        return;
    }
    requests[count++] = bytes;
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
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete[](memory); }

namespace {

namespace seq = core::state::sequencer;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

uint32_t g_now_ms = 0U;
uint32_t mockTimeMs() { return g_now_ms; }

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

constexpr std::size_t expectedOpenAllocationCount(PayloadKind kind) {
    return 2U + (hasGraph(kind) ? 4U : 0U) + (hasCc(kind) ? 4U : 0U);
}

struct ExpectedAllocationRequests {
    std::array<std::size_t, allocation_trace::MAX_REQUESTS> bytes{};
    std::size_t count = 0U;

    void push(std::size_t bytesToAllocate) {
        assert(count < bytes.size());
        bytes[count++] = bytesToAllocate;
    }
};

ExpectedAllocationRequests expectedOpenAllocationRequests(PayloadKind kind) {
    ExpectedAllocationRequests expected;
    expected.push(sizeof(seq::SequencerQuickControlsDraft));
    if (hasGraph(kind)) {
        expected.push(sizeof(oc::note::sequencer::StepSequencerGraph));
    }
    if (hasCc(kind)) expected.push(sizeof(seq::SequencerCcLaneBank));
    expected.push(sizeof(seq::SequencerHistoryPatternChange));
    for (uint8_t copy = 0U; copy < 3U; ++copy) {
        if (hasGraph(kind)) {
            expected.push(sizeof(oc::note::sequencer::StepSequencerGraph));
        }
        if (hasCc(kind)) expected.push(sizeof(seq::SequencerCcLaneBank));
    }
    return expected;
}

void assertOpenAllocationRequests(PayloadKind kind) {
    const auto expected = expectedOpenAllocationRequests(kind);
    assert(!allocation_trace::overflow);
    if (allocation_trace::count != expected.count) {
        std::cerr << "Open allocation count mismatch: expected="
                  << expected.count << " actual=" << allocation_trace::count << '\n';
        for (std::size_t index = 0U; index < allocation_trace::count; ++index) {
            std::cerr << "  request[" << index << "]="
                      << allocation_trace::requests[index] << '\n';
        }
    }
    assert(allocation_trace::count == expected.count);
    for (std::size_t index = 0U; index < expected.count; ++index) {
        assert(allocation_trace::requests[index] == expected.bytes[index]);
    }
}

struct Harness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 1701;

    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers> navigationFocus;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    TestButtonHardware buttonHw;
    TestEncoderHardware encoderHw;
    oc::api::ButtonAPI buttons;
    oc::api::EncoderAPI encoders;
    oc::context::OverlayManager<core::ui::OverlayType> overlayManager;
    core::handler::SequencerPatternQuickControlsHandler quickControls;

    Harness()
        : state(storages.settings)
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlayManager(state.overlays, buttons)
        , quickControls(
              core::handler::SequencerPatternQuickControlsHandler::StateRefs{
                  state.overlays,
                  state.sequencer,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
              encoders,
              buttons,
              SEQUENCER_SCOPE
          ) {
        overlayManager.setActiveViewProvider([]() { return SEQUENCER_SCOPE; });
        g_now_ms = 0U;
    }

    void press(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, true);
        eventBus.emit(oc::core::event::ButtonPressEvent(buttonId, true));
    }

    void release(Config::ButtonID id) {
        const auto buttonId = static_cast<oc::type::ButtonID>(id);
        buttonHw.setPressed(buttonId, false);
        eventBus.emit(oc::core::event::ButtonReleaseEvent(buttonId));
    }

    void tap(Config::ButtonID id) {
        press(id);
        release(id);
    }

    void advance(uint32_t ms) {
        g_now_ms += ms;
        inputBinding.processTick();
    }

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }
};

using RejectionInvariant =
    test_support::sequencer_transaction::StateInvariant;

void preparePayload(Harness& h, PayloadKind kind) {
    auto& pattern = h.state.sequencer.pattern;
    pattern.setContentLength(8U);
    for (uint8_t step = 0U; step < 8U; ++step) pattern.setEnabled(step, false);
    assert(pattern.setStepDataAt(
        0U, 60U, 91U, seq::SequencerPatternState::DEFAULT_GATE_PERCENT));
    pattern.setEnabled(0U, true);

    if (hasGraph(kind)) {
        assert(seq::ensureGraphRoot(pattern));
        assert(seq::setNodeNoteOffset(pattern, seq::rootStepNodeId(0U), 5));
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

void settlePreparedFixture(Harness& h) {
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    test_support::drainNotifications();
    h.state.flushProjectMutationCoalescing();
    h.state.acknowledgeProjectSessionSave(
        h.state.project.metadata.modifiedCounter);
    assert(!h.state.hasPendingProjectSessionSave());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
}

void captureMusical(
    const Harness& h,
    seq::SequencerHistoryPatternSnapshot& out
) {
    test_support::sequencer_transaction::captureMusicalSnapshot(h.state, out);
}

void assertMusicalEquals(
    const Harness& h,
    const seq::SequencerHistoryPatternSnapshot& expected
) {
    test_support::sequencer_transaction::assertMusicalSnapshot(h.state, expected);
}

void assertLivePatternMatchesWithoutAllocation(
    const Harness& h,
    const seq::SequencerHistoryPatternSnapshot& expected
) {
    assert(seq::liveHistoryPatternSnapshotMatches(
        h.state.sequencer.pattern,
        expected));
}

void assertPatternPayloadAtOffset(
    const seq::SequencerPatternState& pattern,
    PayloadKind kind,
    uint8_t expectedStep
) {
    assert(expectedStep < 8U);
    for (uint8_t step = 0U; step < 8U; ++step) {
        assert(pattern.isEnabled(step) == (step == expectedStep));
    }
    assert(pattern.note[expectedStep] == 60U);
    assert(pattern.velocity[expectedStep] == 91U);

    const auto* graph = seq::graphView(pattern);
    assert((graph != nullptr) == hasGraph(kind));
    if (graph != nullptr) {
        for (uint8_t step = 0U; step < 8U; ++step) {
            const auto* node = graph->stepNode(seq::rootStepNodeId(step));
            assert(node != nullptr);
            const bool marker =
                node->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET) &&
                node->noteOffset == 5;
            assert(marker == (step == expectedStep));
        }
    }

    const auto* cc = seq::sequencerCcLaneView(pattern);
    assert((cc != nullptr) == hasCc(kind));
    if (cc != nullptr) {
        for (uint8_t step = 0U; step < 8U; ++step) {
            assert(cc->lanes[0].activeMask.test(step) == (step == expectedStep));
        }
        assert(cc->lanes[0].values[expectedStep] == 99U);
    }
}

const seq::SequencerPatternState& previewPattern(const Harness& h) {
    const auto* preview = h.state.sequencer.quickControlsDraft.previewPattern();
    assert(preview != nullptr);
    return *preview;
}

void holdOpen(Harness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000U);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencer.quickControlsDraft.active());
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
}

void navigateToDivision(Harness& h) {
    h.turn(Config::EncoderID::NAV, 1.0F);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        seq::PatternQuickControlItem::DIVISION);
}

void navigateToOffset(Harness& h) {
    navigateToDivision(h);
    h.turn(Config::EncoderID::NAV, 1.0F);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        seq::PatternQuickControlItem::OFFSET);
}

float normalizedOffset(int offsetSteps) {
    constexpr int maxOffset = 7;
    assert(offsetSteps >= -maxOffset && offsetSteps <= maxOffset);
    return static_cast<float>(offsetSteps + maxOffset) /
           static_cast<float>(maxOffset * 2);
}

float normalizedRootLength(uint8_t length) {
    assert(length >= 1U && length <= seq::SequencerState::MAX_STEPS);
    return static_cast<float>(length - 1U) /
           static_cast<float>(seq::SequencerState::MAX_STEPS - 1U);
}

void assertHistoryRejection(
    const Harness& h,
    const char* expectedDetail,
    uint32_t expectedRevision
) {
    const auto& feedback = h.state.sequencer.historyFeedback;
    assert(feedback.visible.get());
    if (feedback.revision.get() != expectedRevision) {
        std::cerr << "feedback revision mismatch: expected="
                  << expectedRevision << " actual="
                  << feedback.revision.get() << " detail="
                  << feedback.line2.data() << '\n';
    }
    assert(feedback.revision.get() == expectedRevision);
    assert(std::strcmp(feedback.line1.data(), "EDIT BLOCKED") == 0);
    assert(std::strcmp(feedback.line2.data(), expectedDetail) == 0);
    assert(std::strcmp(feedback.line3.data(), "State unchanged") == 0);
}

void test_direct_failure_is_atomic() {
    Harness h;
    preparePayload(h, PayloadKind::FlatOnly);
    assert(seq::initializeTrackBankFromActive(
        h.state.sequencerTracks, h.state.sequencer));
    h.state.sequencer.patternQuickControls.focusedItem.set(
        seq::PatternQuickControlItem::SWING);

    const auto invariant =
        test_support::sequencer_transaction::captureStateInvariant(h.state);
    seq::SequencerHistoryPatternSnapshot before;
    captureMusical(h, before);
    const uint32_t feedbackRevision =
        h.state.sequencer.historyFeedback.revision.get();

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.turn(Config::EncoderID::OPT, 1.0F);
        test_support::sequencer_transaction::assertFailureConsumed(1U);
    }

    test_support::sequencer_transaction::assertStateInvariant(h.state, invariant);
    assertMusicalEquals(h, before);
    assertHistoryRejection(h, "Memory unavailable", feedbackRevision + 1U);
    std::cout << "[PASS] direct Quick Controls fail-1 is atomic\n";
}

void test_direct_graph_cc_offset_is_undoable() {
    Harness h;
    preparePayload(h, PayloadKind::GraphAndCc);
    assert(seq::initializeTrackBankFromActive(
        h.state.sequencerTracks, h.state.sequencer));
    h.state.sequencer.patternQuickControls.focusedItem.set(
        seq::PatternQuickControlItem::OFFSET);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(8U);
        h.turn(Config::EncoderID::OPT, normalizedOffset(1));
        test_support::sequencer_transaction::assertMaxPlusOneStillArmed(7U);
        assert(h.state.commitSequencerPatternHistoryCoalescing());
        test_support::sequencer_transaction::assertMaxPlusOneStillArmed(7U);
    }

    assert(h.state.sequencerHistory.undoCount() == 1U);
    assertPatternPayloadAtOffset(
        h.state.sequencer.pattern, PayloadKind::GraphAndCc, 1U);
    assert(h.state.undoSequencerHistory());
    assertPatternPayloadAtOffset(
        h.state.sequencer.pattern, PayloadKind::GraphAndCc, 0U);
    assert(h.state.redoSequencerHistory());
    assertPatternPayloadAtOffset(
        h.state.sequencer.pattern, PayloadKind::GraphAndCc, 1U);
    std::cout << "[PASS] direct Graph+CC Offset remains undoable\n";
}

void verifyOpenFailure(PayloadKind kind, std::size_t ordinal) {
    Harness h;
    preparePayload(h, kind);
    const auto invariant =
        test_support::sequencer_transaction::captureStateInvariant(h.state);
    seq::SequencerHistoryPatternSnapshot before;
    captureMusical(h, before);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        h.press(Config::ButtonID::LEFT_CENTER);
        h.advance(1000U);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assert(!h.state.sequencer.quickControlsDraft.active());
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        test_support::sequencer_transaction::assertFailureConsumed(ordinal);
    }

    test_support::sequencer_transaction::assertStateInvariant(h.state, invariant);
    assertMusicalEquals(h, before);
}

void test_open_allocation_contract_and_failure_matrix() {
    for (const auto kind : {
             PayloadKind::FlatOnly,
             PayloadKind::GraphOnly,
             PayloadKind::CcOnly,
             PayloadKind::GraphAndCc,
         }) {
        for (std::size_t ordinal = 1U;
             ordinal <= expectedOpenAllocationCount(kind);
             ++ordinal) {
            verifyOpenFailure(kind, ordinal);
        }

        Harness h;
        preparePayload(h, kind);
        {
            allocation_trace::Scope trace;
            core::app::testing::ScopedExtmemAllocationFailure failure(
                expectedOpenAllocationCount(kind) + 1U);
            holdOpen(h);
            assertOpenAllocationRequests(kind);
            test_support::sequencer_transaction::assertMaxPlusOneStillArmed(
                expectedOpenAllocationCount(kind));
            h.tap(Config::ButtonID::LEFT_TOP);
            assert(!h.state.sequencer.patternQuickControls.selecting.get());
            test_support::sequencer_transaction::assertMaxPlusOneStillArmed(
                expectedOpenAllocationCount(kind));
        }
        assert(!h.state.sequencer.quickControlsDraft.active());
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }
    std::cout
        << "[PASS] Open allocation sequence is D/raw owners/Change/Before/After/sync\n";
}

void test_preview_offset_keeps_live_immutable_then_cancel_is_no_write() {
    for (const auto kind : {
             PayloadKind::FlatOnly,
             PayloadKind::GraphOnly,
             PayloadKind::CcOnly,
             PayloadKind::GraphAndCc,
         }) {
        Harness h;
        preparePayload(h, kind);
        seq::SequencerHistoryPatternSnapshot before;
        captureMusical(h, before);
        const auto* liveGraph = h.state.sequencer.pattern.graph.get();
        const auto* liveCc = h.state.sequencer.pattern.ccLanes.get();
        const uint32_t graphRevision =
            h.state.sequencer.pattern.graphRevision.get();
        const uint32_t ccRevision =
            h.state.sequencer.pattern.ccLaneRevision.get();
        h.state.sequencer.page.set(3U);
        h.state.sequencer.focusedStep.set(7U);

        holdOpen(h);
        assert(&seq::authoringPattern(h.state.sequencer) ==
               &previewPattern(h));
        assertPatternPayloadAtOffset(previewPattern(h), kind, 0U);
        assertMusicalEquals(h, before);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            navigateToOffset(h);
            for (const int offset : {1, -2, 0, 2}) {
                h.turn(Config::EncoderID::OPT, normalizedOffset(offset));
                const uint8_t wrapped =
                    static_cast<uint8_t>((offset + 8) % 8);
                assertPatternPayloadAtOffset(previewPattern(h), kind, wrapped);
                assertLivePatternMatchesWithoutAllocation(h, before);
                assert(h.state.sequencer.pattern.graph.get() == liveGraph);
                assert(h.state.sequencer.pattern.ccLanes.get() == liveCc);
                assert(
                    h.state.sequencer.pattern.graphRevision.get() ==
                    graphRevision);
                assert(
                    h.state.sequencer.pattern.ccLaneRevision.get() ==
                    ccRevision);
            }
            h.tap(Config::ButtonID::LEFT_TOP);
            assert(!h.state.sequencer.patternQuickControls.selecting.get());
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }

        assert(!h.state.sequencer.quickControlsDraft.active());
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(h.state.sequencer.page.get() == 3U);
        assert(h.state.sequencer.focusedStep.get() == 7U);
        assertLivePatternMatchesWithoutAllocation(h, before);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }
    std::cout
        << "[PASS] Preview is detached; repeated Offset and Cancel allocate/write zero\n";
}

void test_graph_cc_apply_is_allocation_free_and_undoable() {
    Harness h;
    preparePayload(h, PayloadKind::GraphAndCc);
    assert(seq::initializeTrackBankFromActive(
        h.state.sequencerTracks, h.state.sequencer));
    settlePreparedFixture(h);

    seq::SequencerHistoryPatternSnapshot before;
    captureMusical(h, before);
    const uint32_t modifiedBefore = h.state.project.metadata.modifiedCounter;
    const uint8_t projectUndoBefore = h.state.projectHistory.undoCount();
    const auto* oldLiveGraph = h.state.sequencer.pattern.graph.get();
    const auto* oldLiveCc = h.state.sequencer.pattern.ccLanes.get();

    holdOpen(h);
    navigateToOffset(h);
    h.turn(Config::EncoderID::OPT, normalizedOffset(1));
    h.turn(Config::EncoderID::OPT, normalizedOffset(-2));
    h.turn(Config::EncoderID::OPT, normalizedOffset(0));
    h.turn(Config::EncoderID::OPT, normalizedOffset(2));
    assertPatternPayloadAtOffset(
        previewPattern(h), PayloadKind::GraphAndCc, 2U);
    assertMusicalEquals(h, before);
    const auto* draftGraph = previewPattern(h).graph.get();
    const auto* draftCc = previewPattern(h).ccLanes.get();
    assert(draftGraph != oldLiveGraph);
    assert(draftCc != oldLiveCc);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.release(Config::ButtonID::LEFT_CENTER);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }

    assert(!h.state.sequencer.quickControlsDraft.active());
    assert(h.state.sequencer.pattern.graph.get() == draftGraph);
    assert(h.state.sequencer.pattern.ccLanes.get() == draftCc);
    assertPatternPayloadAtOffset(
        h.state.sequencer.pattern, PayloadKind::GraphAndCc, 2U);
    assertPatternPayloadAtOffset(
        h.state.sequencerTracks.track(0U), PayloadKind::GraphAndCc, 2U);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.projectHistory.undoCount() == projectUndoBefore + 1U);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore + 1U);
    assert(h.state.hasPendingProjectSessionSave());

    seq::SequencerHistoryPatternSnapshot after;
    captureMusical(h, after);
    assert(h.state.undoSequencerHistory());
    assertMusicalEquals(h, before);
    assert(h.state.redoSequencerHistory());
    assertMusicalEquals(h, after);
    std::cout
        << "[PASS] detached Graph+CC Apply swaps owners and publishes one Undo boundary\n";
}

void test_flat_dimensions_preview_and_apply_once() {
    struct Case {
        seq::PatternQuickControlItem item;
        float value;
    };
    const std::array cases{
        Case{seq::PatternQuickControlItem::LENGTH, normalizedRootLength(4U)},
        Case{seq::PatternQuickControlItem::DIVISION, 1.0F},
    };

    for (const auto& testCase : cases) {
        Harness h;
        preparePayload(h, PayloadKind::GraphAndCc);
        assert(seq::initializeTrackBankFromActive(
            h.state.sequencerTracks, h.state.sequencer));
        settlePreparedFixture(h);
        seq::SequencerHistoryPatternSnapshot before;
        captureMusical(h, before);
        const uint8_t liveLength = h.state.sequencer.pattern.length.get();
        const uint8_t liveDivision =
            h.state.sequencer.pattern.stepsPerBeat.get();

        holdOpen(h);
        if (testCase.item == seq::PatternQuickControlItem::DIVISION) {
            navigateToDivision(h);
        }
        h.turn(Config::EncoderID::OPT, testCase.value);
        assert(h.state.sequencer.pattern.length.get() == liveLength);
        assert(
            h.state.sequencer.pattern.stepsPerBeat.get() == liveDivision);
        if (testCase.item == seq::PatternQuickControlItem::LENGTH) {
            assert(previewPattern(h).length.get() == 4U);
        } else {
            assert(previewPattern(h).stepsPerBeat.get() != liveDivision);
        }

        h.release(Config::ButtonID::LEFT_CENTER);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assert(h.state.sequencerHistory.undoCount() == 1U);
        seq::SequencerHistoryPatternSnapshot after;
        captureMusical(h, after);
        assert(!seq::sameMusicalHistorySnapshot(before, after));
        assert(h.state.undoSequencerHistory());
        assertMusicalEquals(h, before);
        assert(h.state.redoSequencerHistory());
        assertMusicalEquals(h, after);
    }
    std::cout << "[PASS] Length and Division preview detached and publish once\n";
}

void test_no_change_apply_restores_opening_view_and_publishes_nothing() {
    Harness h;
    preparePayload(h, PayloadKind::GraphAndCc);
    assert(seq::initializeTrackBankFromActive(
        h.state.sequencerTracks, h.state.sequencer));
    settlePreparedFixture(h);
    h.state.sequencer.page.set(3U);
    h.state.sequencer.focusedStep.set(7U);

    seq::SequencerHistoryPatternSnapshot before;
    captureMusical(h, before);
    const uint32_t modifiedBefore = h.state.project.metadata.modifiedCounter;
    const uint8_t projectUndoBefore = h.state.projectHistory.undoCount();

    holdOpen(h);
    navigateToOffset(h);
    h.turn(Config::EncoderID::OPT, normalizedOffset(2));
    h.turn(Config::EncoderID::OPT, normalizedOffset(0));
    assertPatternPayloadAtOffset(
        previewPattern(h), PayloadKind::GraphAndCc, 0U);
    h.state.sequencer.page.set(0U);
    h.state.sequencer.focusedStep.set(1U);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.release(Config::ButtonID::LEFT_CENTER);
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }

    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(!h.state.sequencer.quickControlsDraft.active());
    assert(h.state.sequencer.page.get() == 3U);
    assert(h.state.sequencer.focusedStep.get() == 7U);
    assertMusicalEquals(h, before);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.projectHistory.undoCount() == projectUndoBefore);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore);
    assert(!h.state.hasPendingProjectSessionSave());
    std::cout << "[PASS] no-change Apply restores view and publishes nothing\n";
}

void test_failed_apply_rearms_without_losing_draft() {
    Harness h;
    preparePayload(h, PayloadKind::FlatOnly);
    seq::SequencerHistoryPatternSnapshot before;
    captureMusical(h, before);
    holdOpen(h);
    h.turn(Config::EncoderID::OPT, normalizedRootLength(4U));
    assert(previewPattern(h).length.get() == 4U);
    assert(h.state.sequencer.pattern.length.get() == 8U);
    assert(
        h.state.abortSequencerPreparedPatternEdit(
            seq::SequencerPreparedPatternEditOwner::QuickControls,
            0U) == seq::SequencerPreparedPatternEditAbortOutcome::Aborted);

    const uint32_t feedbackRevision =
        h.state.sequencer.historyFeedback.revision.get();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.release(Config::ButtonID::LEFT_CENTER);
        assert(h.state.sequencer.patternQuickControls.selecting.get());
        assert(h.state.sequencer.quickControlsDraft.active());
        assert(previewPattern(h).length.get() == 4U);
        assertMusicalEquals(h, before);
        test_support::sequencer_transaction::assertFailureConsumed(1U);
    }
    assertHistoryRejection(
        h, "Memory unavailable", feedbackRevision + 2U);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(!h.state.sequencer.quickControlsDraft.active());
    assert(h.state.sequencer.pattern.length.get() == 4U);
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoSequencerHistory());
    assertMusicalEquals(h, before);
    std::cout << "[PASS] failed Apply keeps exact draft and rearms transaction\n";
}

void test_nested_step_draft_is_transactional_without_live_history() {
    Harness h;
    preparePayload(h, PayloadKind::GraphAndCc);
    seq::SequencerHistoryPatternSnapshot liveBefore;
    captureMusical(h, liveBefore);

    assert(seq::beginStepContentDraft(
        h.state.sequencer,
        seq::SequencerStepContentDraftKind::MICRO_SEQUENCE,
        0U));
    auto* parent = h.state.sequencer.stepContentDraft.pattern();
    assert(parent != nullptr);
    const auto created = seq::createMicroSequence(
        *parent,
        seq::rootStepNodeId(0U),
        2U);
    assert(created.ok);
    seq::notifyStepContentDraftMutation(h.state.sequencer);
    assert(seq::enterMicroSequenceContentView(
        h.state.sequencer,
        seq::rootStepNodeId(0U),
        created.id));

    const auto sequenceLength = [&](const seq::SequencerPatternState& pattern) {
        const auto* graph = seq::graphView(pattern);
        assert(graph != nullptr);
        const auto* sequence = graph->sequence(created.id);
        assert(sequence != nullptr);
        return sequence->length;
    };
    assert(sequenceLength(*parent) == 2U);

    const uint32_t parentGraphRevision = parent->graphRevision.get();
    const uint32_t parentCcRevision = parent->ccLaneRevision.get();
    const auto* parentGraphOwner = parent->graph.get();
    const auto* parentCcOwner = parent->ccLanes.get();
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000U);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(seq::sameMusicalPatternState(*parent, previewPattern(h)));
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(parent->graphRevision.get() == parentGraphRevision);
    assert(parent->ccLaneRevision.get() == parentCcRevision);
    assert(parent->graph.get() == parentGraphOwner);
    assert(parent->ccLanes.get() == parentCcOwner);
    assert(sequenceLength(*parent) == 2U);

    {
        allocation_trace::Scope trace;
        core::app::testing::ScopedExtmemAllocationFailure failure(3U);
        h.press(Config::ButtonID::LEFT_CENTER);
        h.advance(1000U);
        assert(h.state.sequencer.patternQuickControls.selecting.get());
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        assert(allocation_trace::count == 2U);
        assert(
            allocation_trace::requests[0] ==
            sizeof(seq::SequencerQuickControlsDraft));
        assert(
            allocation_trace::requests[1] ==
            sizeof(oc::note::sequencer::StepSequencerGraph));
        test_support::sequencer_transaction::assertMaxPlusOneStillArmed(2U);
        h.turn(Config::EncoderID::OPT, 1.0F);
        assert(sequenceLength(previewPattern(h)) == 16U);
        assert(sequenceLength(*parent) == 2U);
        assertLivePatternMatchesWithoutAllocation(h, liveBefore);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assert(sequenceLength(*parent) == 2U);
        test_support::sequencer_transaction::assertMaxPlusOneStillArmed(2U);
    }
    h.release(Config::ButtonID::LEFT_CENTER);

    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000U);
    h.turn(Config::EncoderID::OPT, 1.0F);
    assert(sequenceLength(previewPattern(h)) == 16U);
    assert(sequenceLength(*parent) == 2U);
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(sequenceLength(*parent) == 16U);
    assert(h.state.sequencer.stepContentDraft.modified());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assertMusicalEquals(h, liveBefore);

    std::cout
        << "[PASS] nested Step draft Apply/Cancel never publish live History\n";
}

void test_raw_disabled_graph_and_empty_cc_owners_are_preserved() {
    Harness h;
    preparePayload(h, PayloadKind::FlatOnly);
    h.state.sequencer.pattern.graph =
        core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>();
    h.state.sequencer.pattern.ccLanes =
        core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
    assert(h.state.sequencer.pattern.graph != nullptr);
    assert(seq::isCanonicalDisabledSequencerGraph(
        *h.state.sequencer.pattern.graph));
    assert(h.state.sequencer.pattern.ccLanes != nullptr);
    assert(seq::sequencerCcLaneCount(
        *h.state.sequencer.pattern.ccLanes) == 0U);
    const auto* liveGraph = h.state.sequencer.pattern.graph.get();
    const auto* liveCc = h.state.sequencer.pattern.ccLanes.get();

    holdOpen(h);
    assert(previewPattern(h).graph != nullptr);
    assert(seq::isCanonicalDisabledSequencerGraph(
        *previewPattern(h).graph));
    assert(previewPattern(h).ccLanes != nullptr);
    assert(seq::sequencerCcLaneCount(*previewPattern(h).ccLanes) == 0U);
    h.tap(Config::ButtonID::LEFT_TOP);

    assert(h.state.sequencer.pattern.graph.get() == liveGraph);
    assert(h.state.sequencer.pattern.ccLanes.get() == liveCc);
    assert(seq::isCanonicalDisabledSequencerGraph(
        *h.state.sequencer.pattern.graph));
    assert(seq::sequencerCcLaneCount(
        *h.state.sequencer.pattern.ccLanes) == 0U);
    assert(h.state.sequencerHistory.undoCount() == 0U);
    std::cout << "[PASS] raw disabled Graph and empty CC owner presence survives Cancel\n";
}

}  // namespace

int main() {
    std::cout << std::unitbuf;
    static_assert(
        sizeof(seq::SequencerQuickControlsDraftSession) == sizeof(void*));

    test_direct_failure_is_atomic();
    test_direct_graph_cc_offset_is_undoable();
    test_open_allocation_contract_and_failure_matrix();
    test_preview_offset_keeps_live_immutable_then_cancel_is_no_write();
    test_graph_cc_apply_is_allocation_free_and_undoable();
    test_flat_dimensions_preview_and_apply_once();
    test_no_change_apply_restores_opening_view_and_publishes_nothing();
    test_failed_apply_rearms_without_losing_draft();
    test_nested_step_draft_is_transactional_without_live_history();
    test_raw_disabled_graph_and_empty_cc_owners_are_preserved();
    std::cout << "All detached Sequencer Quick Controls tests passed.\n";
    return 0;
}
