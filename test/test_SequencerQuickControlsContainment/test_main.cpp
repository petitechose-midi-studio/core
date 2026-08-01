#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

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
#include "support/CoreStorages.hpp"
#include "support/InputTestHardware.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace {

namespace seq = core::state::sequencer;
using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;

uint32_t g_now_ms = 0U;

uint32_t mockTimeMs() {
    return g_now_ms;
}

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

struct RejectionInvariant {
    const void* graphOwner = nullptr;
    const void* ccOwner = nullptr;
    uint32_t graphRevision = 0U;
    uint32_t ccLaneRevision = 0U;
    uint32_t modifiedCounter = 0U;
    bool dirty = false;
    uint8_t undoCount = 0U;
    uint8_t redoCount = 0U;
};

RejectionInvariant captureRejectionInvariant(const Harness& h) {
    return {
        .graphOwner = h.state.sequencer.pattern.graph.get(),
        .ccOwner = h.state.sequencer.pattern.ccLanes.get(),
        .graphRevision = h.state.sequencer.pattern.graphRevision.get(),
        .ccLaneRevision = h.state.sequencer.pattern.ccLaneRevision.get(),
        .modifiedCounter = h.state.project.metadata.modifiedCounter,
        .dirty = h.state.project.metadata.dirty,
        .undoCount = h.state.sequencerHistory.undoCount(),
        .redoCount = h.state.sequencerHistory.redoCount(),
    };
}

void assertRejectionInvariant(
    const Harness& h,
    const RejectionInvariant& expected
) {
    const auto actual = captureRejectionInvariant(h);
    assert(actual.graphOwner == expected.graphOwner);
    assert(actual.ccOwner == expected.ccOwner);
    assert(actual.graphRevision == expected.graphRevision);
    assert(actual.ccLaneRevision == expected.ccLaneRevision);
    assert(actual.modifiedCounter == expected.modifiedCounter);
    assert(actual.dirty == expected.dirty);
    assert(actual.undoCount == expected.undoCount);
    assert(actual.redoCount == expected.redoCount);
}

void preparePayload(Harness& h, PayloadKind kind) {
    auto& pattern = h.state.sequencer.pattern;
    pattern.setContentLength(8U);
    assert(pattern.length.get() == 8U);
    for (uint8_t step = 0U; step < 8U; ++step) {
        pattern.setEnabled(step, false);
    }
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

void captureMusical(
    const Harness& h,
    seq::SequencerHistoryPatternSnapshot& out
) {
    assert(seq::captureHistorySnapshot(h.state.sequencer, out));
}

void assertMusicalEquals(
    const Harness& h,
    const seq::SequencerHistoryPatternSnapshot& expected
) {
    seq::SequencerHistoryPatternSnapshot actual;
    captureMusical(h, actual);
    assert(seq::sameMusicalHistorySnapshot(actual, expected));
}

void assertPayloadAtOffset(
    const Harness& h,
    PayloadKind kind,
    uint8_t expectedStep
) {
    const auto& pattern = h.state.sequencer.pattern;
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
            const bool isMarker =
                node->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET) &&
                node->noteOffset == 5;
            assert(isMarker == (step == expectedStep));
        }
    }

    const auto* cc = seq::sequencerCcLaneView(pattern);
    assert((cc != nullptr) == hasCc(kind));
    if (cc != nullptr) {
        assert(cc->lanes[0].occupied);
        for (uint8_t step = 0U; step < 8U; ++step) {
            const bool isMarker = cc->lanes[0].activeMask.test(step);
            assert(isMarker == (step == expectedStep));
        }
        assert(cc->lanes[0].values[expectedStep] == 99U);
    }
}

void holdOpen(Harness& h) {
    h.press(Config::ButtonID::LEFT_CENTER);
    h.advance(1000U);
    assert(h.state.sequencer.patternQuickControls.selecting.get());
}

void navigateToDivision(Harness& h) {
    h.turn(Config::EncoderID::NAV, 1.0F);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        seq::PatternQuickControlItem::DIVISION
    );
}

void navigateToOffset(Harness& h) {
    navigateToDivision(h);
    h.turn(Config::EncoderID::NAV, 1.0F);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        seq::PatternQuickControlItem::OFFSET
    );
}

float normalizedOffset(int offsetSteps) {
    constexpr int maxOffset = 7;
    assert(offsetSteps >= -maxOffset && offsetSteps <= maxOffset);
    return static_cast<float>(offsetSteps + maxOffset) /
           static_cast<float>(maxOffset * 2);
}

float normalizedOffsetForLength(int offsetSteps, uint8_t length) {
    const int maxOffset = static_cast<int>(length) - 1;
    assert(maxOffset > 0);
    assert(offsetSteps >= -maxOffset && offsetSteps <= maxOffset);
    return static_cast<float>(offsetSteps + maxOffset) /
           static_cast<float>(maxOffset * 2);
}

void assertFailureWasConsumed(std::size_t ordinal) {
    assert(core::app::testing::extmemAllocationAttempt == ordinal);
    assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
}

void assertAllocationRatchet(std::size_t expectedAttempts) {
    assert(core::app::testing::extmemAllocationAttempt == expectedAttempts);
    assert(
        core::app::testing::extmemAllocationFailureOrdinal ==
        expectedAttempts + 1U
    );
}

void test_graphless_open_performs_no_extmem_allocation() {
    Harness h;
    preparePayload(h, PayloadKind::FlatOnly);
    seq::SequencerHistoryPatternSnapshot baseline;
    captureMusical(h, baseline);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.press(Config::ButtonID::LEFT_CENTER);
        h.advance(1000U);
        assert(h.state.sequencer.patternQuickControls.selecting.get());
        assert(core::app::testing::extmemAllocationAttempt == 0U);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assertMusicalEquals(h, baseline);
    assert(h.state.sequencerHistory.undoCount() == 0U);

    std::cout << "[PASS] graphless open performs zero EXTMEM allocations\n";
}

void test_graphless_offset_cancel_and_apply_are_allocation_free() {
    {
        Harness h;
        preparePayload(h, PayloadKind::FlatOnly);
        seq::SequencerHistoryPatternSnapshot baseline;
        captureMusical(h, baseline);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            holdOpen(h);
            navigateToOffset(h);
            h.turn(Config::EncoderID::OPT, normalizedOffset(1));
            assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
            assertPayloadAtOffset(h, PayloadKind::FlatOnly, 1U);
            h.turn(Config::EncoderID::OPT, normalizedOffset(0));
            assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 0);
            assertPayloadAtOffset(h, PayloadKind::FlatOnly, 0U);
            assertMusicalEquals(h, baseline);
            h.turn(Config::EncoderID::OPT, normalizedOffset(2));
            assertPayloadAtOffset(h, PayloadKind::FlatOnly, 2U);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }

        // Apply's normal History record is outside the zero-allocation restore
        // assertion and remains owned by L-R08-08.
        h.release(Config::ButtonID::LEFT_CENTER);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assertPayloadAtOffset(h, PayloadKind::FlatOnly, 2U);
        assert(h.state.sequencerHistory.undoCount() == 1U);
    }

    {
        Harness h;
        preparePayload(h, PayloadKind::FlatOnly);
        seq::SequencerHistoryPatternSnapshot baseline;
        captureMusical(h, baseline);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            holdOpen(h);
            navigateToOffset(h);
            h.turn(Config::EncoderID::OPT, normalizedOffset(1));
            assertPayloadAtOffset(h, PayloadKind::FlatOnly, 1U);
            h.tap(Config::ButtonID::LEFT_TOP);
            assert(!h.state.sequencer.patternQuickControls.selecting.get());
            assertPayloadAtOffset(h, PayloadKind::FlatOnly, 0U);
            assertMusicalEquals(h, baseline);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }

        h.release(Config::ButtonID::LEFT_CENTER);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }

    std::cout << "[PASS] graphless Offset, Cancel, and Apply preserve zero-allocation semantics\n";
}

void verifyOpenFailure(PayloadKind kind, std::size_t ordinal) {
    Harness h;
    preparePayload(h, kind);
    seq::SequencerHistoryPatternSnapshot baseline;
    captureMusical(h, baseline);
    const auto invariant = captureRejectionInvariant(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        h.press(Config::ButtonID::LEFT_CENTER);
        h.advance(1000U);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assertFailureWasConsumed(ordinal);
        assertRejectionInvariant(h, invariant);
    }

    h.release(Config::ButtonID::LEFT_CENTER);
    assertMusicalEquals(h, baseline);
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void verifyOpenAllocationRatchet(PayloadKind kind, std::size_t expectedAttempts) {
    Harness h;
    preparePayload(h, kind);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        h.press(Config::ButtonID::LEFT_CENTER);
        h.advance(1000U);
        assert(h.state.sequencer.patternQuickControls.selecting.get());
        assertAllocationRatchet(expectedAttempts);
    }

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void test_open_failure_matrix_is_atomic() {
    for (std::size_t ordinal = 1U; ordinal <= 3U; ++ordinal) {
        verifyOpenFailure(PayloadKind::GraphOnly, ordinal);
        verifyOpenFailure(PayloadKind::CcOnly, ordinal);
    }
    for (std::size_t ordinal = 1U; ordinal <= 6U; ++ordinal) {
        verifyOpenFailure(PayloadKind::GraphAndCc, ordinal);
    }
    verifyOpenAllocationRatchet(PayloadKind::GraphOnly, 3U);
    verifyOpenAllocationRatchet(PayloadKind::CcOnly, 3U);
    verifyOpenAllocationRatchet(PayloadKind::GraphAndCc, 6U);

    std::cout << "[PASS] open failure matrix is atomic at every allocation ordinal\n";
}

void verifyOffsetEntryFailure(PayloadKind kind, std::size_t ordinal) {
    Harness h;
    preparePayload(h, kind);
    holdOpen(h);
    navigateToDivision(h);
    seq::SequencerHistoryPatternSnapshot beforeEntry;
    captureMusical(h, beforeEntry);
    const auto invariant = captureRejectionInvariant(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        h.turn(Config::EncoderID::NAV, 1.0F);
        assert(
            h.state.sequencer.patternQuickControls.focusedItem.get() ==
            seq::PatternQuickControlItem::DIVISION
        );
        assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 0);
        assertFailureWasConsumed(ordinal);
        assertRejectionInvariant(h, invariant);
    }

    assertMusicalEquals(h, beforeEntry);
    h.turn(Config::EncoderID::NAV, 1.0F);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        seq::PatternQuickControlItem::OFFSET
    );
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void verifyOffsetEntryAllocationRatchet(
    PayloadKind kind,
    std::size_t expectedAttempts
) {
    Harness h;
    preparePayload(h, kind);
    holdOpen(h);
    navigateToDivision(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        h.turn(Config::EncoderID::NAV, 1.0F);
        assert(
            h.state.sequencer.patternQuickControls.focusedItem.get() ==
            seq::PatternQuickControlItem::OFFSET
        );
        assertAllocationRatchet(expectedAttempts);
    }

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void test_offset_entry_failure_matrix_is_retryable() {
    verifyOffsetEntryFailure(PayloadKind::GraphOnly, 1U);
    verifyOffsetEntryFailure(PayloadKind::CcOnly, 1U);
    verifyOffsetEntryFailure(PayloadKind::GraphAndCc, 1U);
    verifyOffsetEntryFailure(PayloadKind::GraphAndCc, 2U);
    verifyOffsetEntryAllocationRatchet(PayloadKind::GraphOnly, 1U);
    verifyOffsetEntryAllocationRatchet(PayloadKind::CcOnly, 1U);
    verifyOffsetEntryAllocationRatchet(PayloadKind::GraphAndCc, 2U);

    std::cout << "[PASS] Offset entry failure matrix is atomic and retryable\n";
}

void verifyCancelFailure(PayloadKind kind, std::size_t ordinal) {
    Harness h;
    preparePayload(h, kind);
    seq::SequencerHistoryPatternSnapshot baseline;
    captureMusical(h, baseline);

    holdOpen(h);
    navigateToOffset(h);
    h.turn(Config::EncoderID::OPT, normalizedOffset(1));
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
    assertPayloadAtOffset(h, kind, 1U);
    seq::SequencerHistoryPatternSnapshot liveEdit;
    captureMusical(h, liveEdit);
    const auto invariant = captureRejectionInvariant(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(h.state.sequencer.patternQuickControls.selecting.get());
        assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
        assertPayloadAtOffset(h, kind, 1U);
        assertFailureWasConsumed(ordinal);
        assertRejectionInvariant(h, invariant);

        // A physical release after the failed Cancel must be consumed without
        // applying/closing. The explicit Cancel retry remains authoritative.
        h.release(Config::ButtonID::LEFT_CENTER);
        assert(h.state.sequencer.patternQuickControls.selecting.get());
        assertPayloadAtOffset(h, kind, 1U);
        assertFailureWasConsumed(ordinal);
        assertRejectionInvariant(h, invariant);
    }

    assertMusicalEquals(h, liveEdit);
    h.tap(Config::ButtonID::LEFT_TOP);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assertPayloadAtOffset(h, kind, 0U);
    assertMusicalEquals(h, baseline);
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void verifyCancelAllocationRatchet(PayloadKind kind, std::size_t expectedAttempts) {
    Harness h;
    preparePayload(h, kind);
    seq::SequencerHistoryPatternSnapshot baseline;
    captureMusical(h, baseline);
    holdOpen(h);
    navigateToOffset(h);
    h.turn(Config::EncoderID::OPT, normalizedOffset(1));

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(!h.state.sequencer.patternQuickControls.selecting.get());
        assertAllocationRatchet(expectedAttempts);
    }

    h.release(Config::ButtonID::LEFT_CENTER);
    assertMusicalEquals(h, baseline);
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void test_cancel_failure_matrix_preserves_retry_contract() {
    verifyCancelFailure(PayloadKind::GraphOnly, 1U);
    verifyCancelFailure(PayloadKind::CcOnly, 1U);
    verifyCancelFailure(PayloadKind::GraphAndCc, 1U);
    verifyCancelFailure(PayloadKind::GraphAndCc, 2U);
    verifyCancelAllocationRatchet(PayloadKind::GraphOnly, 1U);
    verifyCancelAllocationRatchet(PayloadKind::CcOnly, 1U);
    verifyCancelAllocationRatchet(PayloadKind::GraphAndCc, 2U);

    std::cout << "[PASS] Cancel failure matrix preserves state and explicit retry\n";
}

void verifyOffsetRestoreFailure(PayloadKind kind, std::size_t ordinal) {
    Harness h;
    preparePayload(h, kind);
    seq::SequencerHistoryPatternSnapshot baseline;
    captureMusical(h, baseline);

    holdOpen(h);
    navigateToOffset(h);
    h.turn(Config::EncoderID::OPT, normalizedOffset(1));
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
    assertPayloadAtOffset(h, kind, 1U);
    seq::SequencerHistoryPatternSnapshot offsetOne;
    captureMusical(h, offsetOne);
    const auto invariant = captureRejectionInvariant(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        h.turn(Config::EncoderID::OPT, normalizedOffset(2));
        assert(h.state.sequencer.patternQuickControls.selecting.get());
        assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
        assertPayloadAtOffset(h, kind, 1U);
        assertFailureWasConsumed(ordinal);
        assertRejectionInvariant(h, invariant);
    }

    assertMusicalEquals(h, offsetOne);

    h.turn(Config::EncoderID::OPT, normalizedOffset(2));
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 2);
    assertPayloadAtOffset(h, kind, 2U);

    h.turn(Config::EncoderID::OPT, normalizedOffset(0));
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 0);
    assertPayloadAtOffset(h, kind, 0U);
    assertMusicalEquals(h, baseline);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void verifyOffsetRestoreAllocationRatchet(
    PayloadKind kind,
    std::size_t expectedAttempts
) {
    Harness h;
    preparePayload(h, kind);
    holdOpen(h);
    navigateToOffset(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(
            expectedAttempts + 1U
        );
        h.turn(Config::EncoderID::OPT, normalizedOffset(1));
        assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
        assertPayloadAtOffset(h, kind, 1U);
        assertAllocationRatchet(expectedAttempts);
    }

    h.turn(Config::EncoderID::OPT, normalizedOffset(0));
    h.release(Config::ButtonID::LEFT_CENTER);
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void test_offset_restore_failure_matrix_is_atomic_and_reversible() {
    verifyOffsetRestoreFailure(PayloadKind::GraphOnly, 1U);
    verifyOffsetRestoreFailure(PayloadKind::CcOnly, 1U);
    verifyOffsetRestoreFailure(PayloadKind::GraphAndCc, 1U);
    verifyOffsetRestoreFailure(PayloadKind::GraphAndCc, 2U);
    verifyOffsetRestoreAllocationRatchet(PayloadKind::GraphOnly, 1U);
    verifyOffsetRestoreAllocationRatchet(PayloadKind::CcOnly, 1U);
    verifyOffsetRestoreAllocationRatchet(PayloadKind::GraphAndCc, 2U);

    std::cout << "[PASS] Offset restore failure matrix is atomic, retryable, and reversible\n";
}

void prepareChildPayload(Harness& h, PayloadKind kind) {
    assert(hasGraph(kind));
    preparePayload(h, kind);
    const auto micro = seq::createMicroSequence(
        h.state.sequencer.pattern,
        seq::rootStepNodeId(0U),
        4U
    );
    assert(micro.ok);
    assert(seq::enterMicroSequenceContentView(
        h.state.sequencer,
        seq::rootStepNodeId(0U),
        micro.id
    ));
    const auto marker = seq::activeContentStepNodeId(h.state.sequencer, 0U);
    assert(marker != oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID);
    assert(seq::setNodeNoteOffset(h.state.sequencer.pattern, marker, 7));
}

void assertChildMarkerAt(const Harness& h, uint8_t expectedStep) {
    assert(seq::isMicroSequenceContentView(h.state.sequencer));
    assert(seq::activeContentLength(h.state.sequencer) == 4U);
    const auto* graph = seq::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    for (uint8_t step = 0U; step < 4U; ++step) {
        const auto nodeId = seq::activeContentStepNodeId(h.state.sequencer, step);
        const auto* node = graph->stepNode(nodeId);
        assert(node != nullptr);
        const bool isMarker =
            node->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET) &&
            node->noteOffset == 7;
        assert(isMarker == (step == expectedStep));
    }
}

void verifyChildOffsetFailure(PayloadKind kind, std::size_t ordinal) {
    Harness h;
    prepareChildPayload(h, kind);
    seq::SequencerHistoryPatternSnapshot baseline;
    captureMusical(h, baseline);
    assertPayloadAtOffset(h, kind, 0U);
    assertChildMarkerAt(h, 0U);

    holdOpen(h);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        seq::PatternQuickControlItem::LENGTH
    );
    const auto entryInvariant = captureRejectionInvariant(h);
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        h.turn(Config::EncoderID::NAV, 1.0F);
        assert(
            h.state.sequencer.patternQuickControls.focusedItem.get() ==
            seq::PatternQuickControlItem::LENGTH
        );
        assertFailureWasConsumed(ordinal);
        assertRejectionInvariant(h, entryInvariant);
        assertChildMarkerAt(h, 0U);
    }

    h.turn(Config::EncoderID::NAV, 1.0F);
    assert(
        h.state.sequencer.patternQuickControls.focusedItem.get() ==
        seq::PatternQuickControlItem::OFFSET
    );
    h.turn(Config::EncoderID::OPT, normalizedOffsetForLength(1, 4U));
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
    assertChildMarkerAt(h, 1U);
    assertPayloadAtOffset(h, kind, 0U);
    seq::SequencerHistoryPatternSnapshot offsetOne;
    captureMusical(h, offsetOne);
    const auto restoreInvariant = captureRejectionInvariant(h);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
        h.turn(Config::EncoderID::OPT, normalizedOffsetForLength(2, 4U));
        assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
        assertFailureWasConsumed(ordinal);
        assertRejectionInvariant(h, restoreInvariant);
        assertChildMarkerAt(h, 1U);
        assertPayloadAtOffset(h, kind, 0U);
    }

    assertMusicalEquals(h, offsetOne);
    h.turn(Config::EncoderID::OPT, normalizedOffsetForLength(2, 4U));
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 2);
    assertChildMarkerAt(h, 2U);
    assertPayloadAtOffset(h, kind, 0U);

    h.turn(Config::EncoderID::OPT, normalizedOffsetForLength(0, 4U));
    assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 0);
    assertChildMarkerAt(h, 0U);
    assertPayloadAtOffset(h, kind, 0U);
    assertMusicalEquals(h, baseline);

    h.release(Config::ButtonID::LEFT_CENTER);
    assert(!h.state.sequencer.patternQuickControls.selecting.get());
    assert(h.state.sequencerHistory.undoCount() == 0U);
}

void verifyChildAllocationRatchets(
    PayloadKind kind,
    std::size_t expectedAttempts
) {
    {
        Harness h;
        prepareChildPayload(h, kind);
        holdOpen(h);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(
                expectedAttempts + 1U
            );
            h.turn(Config::EncoderID::NAV, 1.0F);
            assert(
                h.state.sequencer.patternQuickControls.focusedItem.get() ==
                seq::PatternQuickControlItem::OFFSET
            );
            assertAllocationRatchet(expectedAttempts);
        }

        h.release(Config::ButtonID::LEFT_CENTER);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }

    {
        Harness h;
        prepareChildPayload(h, kind);
        holdOpen(h);
        h.turn(Config::EncoderID::NAV, 1.0F);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(
                expectedAttempts + 1U
            );
            h.turn(Config::EncoderID::OPT, normalizedOffsetForLength(1, 4U));
            assert(h.state.sequencer.patternQuickControls.offsetSteps.get() == 1);
            assertChildMarkerAt(h, 1U);
            assertAllocationRatchet(expectedAttempts);
        }

        h.turn(Config::EncoderID::OPT, normalizedOffsetForLength(0, 4U));
        h.release(Config::ButtonID::LEFT_CENTER);
        assert(h.state.sequencerHistory.undoCount() == 0U);
    }
}

void test_child_offset_failure_matrix_is_contained() {
    verifyChildOffsetFailure(PayloadKind::GraphOnly, 1U);
    verifyChildOffsetFailure(PayloadKind::GraphAndCc, 1U);
    verifyChildOffsetFailure(PayloadKind::GraphAndCc, 2U);
    verifyChildAllocationRatchets(PayloadKind::GraphOnly, 1U);
    verifyChildAllocationRatchets(PayloadKind::GraphAndCc, 2U);

    std::cout << "[PASS] child Offset navigation and restore failures are contained\n";
}

}  // namespace

int main() {
    test_graphless_open_performs_no_extmem_allocation();
    test_graphless_offset_cancel_and_apply_are_allocation_free();
    test_open_failure_matrix_is_atomic();
    test_offset_entry_failure_matrix_is_retryable();
    test_cancel_failure_matrix_preserves_retry_contract();
    test_offset_restore_failure_matrix_is_atomic_and_reversible();
    test_child_offset_failure_matrix_is_contained();
    std::cout << "All Sequencer Quick Controls containment tests passed.\n";
    return 0;
}
