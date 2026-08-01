#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/core/input/InputBinding.hpp>
#include <oc/state/NotificationQueue.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "../../src/handler/sequencer/SequencerInputUtils.hpp"
#include "../../src/handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerContentViewOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/InputTestHardware.hpp"
#include "../support/SequencerHistoryTransactionAssertions.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace {

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

using test_support::TestButtonHardware;
using test_support::TestEncoderHardware;
using StepProperty = core::state::sequencer::StepProperty;
namespace input_utils = core::handler::sequencer::input_utils;
namespace tx = test_support::sequencer_transaction;

struct MacroUiInvariant {
    bool feedbackVisible = false;
    oc::note::sequencer::StepBitMask128 feedbackTouchedMask{};
    StepProperty feedbackProperty = StepProperty::NOTE;
    std::array<
        uint32_t,
        core::state::sequencer::SequencerStepInlineFeedbackState::MAX_STEPS>
        feedbackHideAt{};
    bool selectorSelecting = false;
    bool selectorLocalVariationActive = false;
    uint8_t selectorLocalVariationStep = 0U;
    bool quickFeedbackVisible = false;
    core::state::sequencer::PatternQuickControlItem quickFocusedItem =
        core::state::sequencer::PatternQuickControlItem::LENGTH;
    int8_t quickOffsetSteps = 0;
    uint32_t quickHideAtMs = 0U;
};

MacroUiInvariant captureMacroUiInvariant(
    const core::state::sequencer::SequencerState& sequencer
) {
    const auto& feedback = sequencer.stepInlineFeedback;
    const auto& selector = sequencer.stepPropertyInlineSelector;
    const auto& quick = sequencer.patternQuickControls;
    MacroUiInvariant invariant{
        .feedbackVisible = feedback.visible.get(),
        .feedbackTouchedMask = feedback.touchedMask.get(),
        .feedbackProperty = feedback.property.get(),
        .selectorSelecting = selector.selecting.get(),
        .selectorLocalVariationActive =
            selector.macroLocalVariationEditActive.get(),
        .selectorLocalVariationStep = selector.localVariationStepIndex,
        .quickFeedbackVisible = quick.feedbackVisible.get(),
        .quickFocusedItem = quick.focusedItem.get(),
        .quickOffsetSteps = quick.offsetSteps.get(),
        .quickHideAtMs = quick.hideAtMs,
    };
    std::memcpy(
        invariant.feedbackHideAt.data(),
        feedback.hideAtMs,
        sizeof(feedback.hideAtMs)
    );
    return invariant;
}

void assertMacroUiInvariant(
    const core::state::sequencer::SequencerState& sequencer,
    const MacroUiInvariant& expected
) {
    const auto actual = captureMacroUiInvariant(sequencer);
    assert(actual.feedbackVisible == expected.feedbackVisible);
    for (uint8_t step = 0U; step < actual.feedbackHideAt.size(); ++step) {
        assert(
            actual.feedbackTouchedMask.test(step) ==
            expected.feedbackTouchedMask.test(step)
        );
    }
    assert(actual.feedbackProperty == expected.feedbackProperty);
    assert(actual.feedbackHideAt == expected.feedbackHideAt);
    assert(actual.selectorSelecting == expected.selectorSelecting);
    assert(
        actual.selectorLocalVariationActive ==
        expected.selectorLocalVariationActive
    );
    assert(
        actual.selectorLocalVariationStep ==
        expected.selectorLocalVariationStep
    );
    assert(actual.quickFeedbackVisible == expected.quickFeedbackVisible);
    assert(actual.quickFocusedItem == expected.quickFocusedItem);
    assert(actual.quickOffsetSteps == expected.quickOffsetSteps);
    assert(actual.quickHideAtMs == expected.quickHideAtMs);
}

struct SequencerMacroPropertyHarness {
    static constexpr oc::type::ScopeID SEQUENCER_SCOPE = 1101;

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
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerMacroPropertyHandler handler;

    SequencerMacroPropertyHarness()
        : state(storages.settings)
        , navigationFocus(core::state::StructureNavigationFocus::PAGE)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , encoders(inputBinding, encoderHw)
        , overlays(state.overlays, buttons)
        , handler(core::handler::SequencerMacroPropertyHandler::StateRefs{
                      state.overlays,
                  state.sequencer,
                  state.sequencerTracks,
                  state.trackNavigation,
                  navigationFocus,
                  core::handler::SequencerHistoryDomainServices::fromCoreState(state),
              },
                  encoders,
                  buttons,
                  SEQUENCER_SCOPE,
                  mockTimeMs) {
        g_now_ms = 0;
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

    void turn(Config::EncoderID id, float value) {
        const auto encoderId = static_cast<oc::type::EncoderID>(id);
        encoderHw.setPosition(encoderId, value);
        eventBus.emit(oc::core::event::EncoderChangedEvent(encoderId, value));
    }

    void advance(uint32_t nowMs) {
        g_now_ms = nowMs;
        state.updateSequencerPatternHistoryCoalescing(nowMs);
    }
};

enum class RejectedMacroCaller : uint8_t {
    MacroLocalVariation = 0U,
    MacroState,
    MacroOrdinary,
    OptState,
    OptOrdinary,
};

void configureRejectedMacroCaller(
    SequencerMacroPropertyHarness& h,
    RejectedMacroCaller caller
) {
    auto& sequencer = h.state.sequencer;
    sequencer.pattern.setContentLength(8U);
    sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    sequencer.pattern.velocity[0U] = 10U;
    sequencer.pattern.velocity[2U] = 20U;

    switch (caller) {
        case RejectedMacroCaller::MacroLocalVariation:
            sequencer.activeStepProperty.set(StepProperty::NOTE);
            sequencer.pattern.note[2U] = 60U;
            sequencer.stepPropertyInlineSelector.selecting.set(true);
            h.press(Config::ButtonID::LEFT_BOTTOM);
            return;
        case RejectedMacroCaller::MacroState:
            sequencer.stepStatePropertyActive.set(true);
            return;
        case RejectedMacroCaller::MacroOrdinary:
            return;
        case RejectedMacroCaller::OptState:
            sequencer.stepStatePropertyActive.set(true);
            sequencer.focusedStep.set(2U);
            h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
            return;
        case RejectedMacroCaller::OptOrdinary:
            sequencer.focusedStep.set(2U);
            h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
            return;
    }
}

void invokeRejectedMacroCaller(
    SequencerMacroPropertyHarness& h,
    RejectedMacroCaller caller
) {
    switch (caller) {
        case RejectedMacroCaller::MacroLocalVariation:
            h.turn(Config::EncoderID::MACRO_3, 1.0F);
            return;
        case RejectedMacroCaller::MacroState:
        case RejectedMacroCaller::MacroOrdinary:
            h.turn(Config::EncoderID::MACRO_1, 1.0F);
            return;
        case RejectedMacroCaller::OptState:
        case RejectedMacroCaller::OptOrdinary:
            h.turn(Config::EncoderID::OPT, 1.0F);
            return;
    }
}

void test_all_five_macro_and_opt_callers_reject_fail_one_atomically() {
    constexpr std::array callers{
        RejectedMacroCaller::MacroLocalVariation,
        RejectedMacroCaller::MacroState,
        RejectedMacroCaller::MacroOrdinary,
        RejectedMacroCaller::OptState,
        RejectedMacroCaller::OptOrdinary,
    };

    for (const auto caller : callers) {
        SequencerMacroPropertyHarness h;
        configureRejectedMacroCaller(h, caller);
        g_now_ms = 100U;

        const auto stateBefore = tx::captureStateInvariant(h.state);
        core::state::sequencer::SequencerHistoryPatternSnapshot musicalBefore;
        tx::captureMusicalSnapshot(h.state, musicalBefore);
        const auto uiBefore = captureMacroUiInvariant(h.state.sequencer);
        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            invokeRejectedMacroCaller(h, caller);
            tx::assertFailureConsumed(1U);
        }

        assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
        tx::assertStateInvariant(h.state, stateBefore);
        tx::assertMusicalSnapshot(h.state, musicalBefore);
        assertMacroUiInvariant(h.state.sequencer, uiBefore);
    }

    std::cout
        << "[PASS] all five Macro/OPT callers reject fail-1 atomically\n";
}

enum class ChildMacroCaller : uint8_t {
    MacroState = 0U,
    MacroOrdinary,
    OptState,
    OptOrdinary,
};

void prepareChildGraphAndCc(
    SequencerMacroPropertyHarness& h,
    ChildMacroCaller caller
) {
    auto& sequencer = h.state.sequencer;
    sequencer.pattern.setContentLength(8U);
    const auto micro = core::state::sequencer::createMicroSequence(
        sequencer.pattern,
        core::state::sequencer::rootStepNodeId(0U),
        4U
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        sequencer,
        core::state::sequencer::rootStepNodeId(0U),
        micro.id
    ));

    const bool stateCaller =
        caller == ChildMacroCaller::MacroState ||
        caller == ChildMacroCaller::OptState;
    sequencer.stepStatePropertyActive.set(stateCaller);
    sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    sequencer.focusedStep.set(0U);
    sequencer.page.set(0U);
    if (stateCaller) {
        (void)core::state::sequencer::setActiveContentStepEnabled(
            sequencer,
            0U,
            false
        );
    }

    auto* cc = core::state::sequencer::ensureSequencerCcLaneBank(
        sequencer.pattern
    );
    assert(cc != nullptr);
    core::state::sequencer::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74U;
    assert(core::state::sequencer::createSequencerCcLane(*cc, 0U, draft).changed());
    assert(core::state::sequencer::setSequencerCcLaneEvent(
        *cc,
        0U,
        0U,
        91U
    ).changed());
    sequencer.pattern.bumpCcLaneRevision();

    if (caller == ChildMacroCaller::OptState ||
        caller == ChildMacroCaller::OptOrdinary) {
        h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    }
    assert(core::state::sequencer::initializeTrackBankFromActive(
        h.state.sequencerTracks,
        sequencer
    ));
}

void invokeChildMacroCaller(
    SequencerMacroPropertyHarness& h,
    ChildMacroCaller caller
) {
    const bool opt = caller == ChildMacroCaller::OptState ||
                     caller == ChildMacroCaller::OptOrdinary;
    h.turn(opt ? Config::EncoderID::OPT : Config::EncoderID::MACRO_1, 1.0F);
}

void assertChildEditorAndBankPayloadMatch(
    const SequencerMacroPropertyHarness& h
) {
    const auto& editor = h.state.sequencer.pattern;
    const auto& bank = h.state.sequencerTracks.track(0U);
    const auto* editorGraph = core::state::sequencer::graphView(editor);
    const auto* bankGraph = core::state::sequencer::graphView(bank);
    const auto* editorCc = core::state::sequencer::sequencerCcLaneView(editor);
    const auto* bankCc = core::state::sequencer::sequencerCcLaneView(bank);
    assert(editorGraph != nullptr && bankGraph != nullptr);
    assert(editorCc != nullptr && bankCc != nullptr);
    assert(editorGraph != bankGraph);
    assert(editorCc != bankCc);
    assert(std::memcmp(editorGraph, bankGraph, sizeof(*editorGraph)) == 0);
    assert(std::memcmp(editorCc, bankCc, sizeof(*editorCc)) == 0);
    assert(editor.stepDataRevision.get() == bank.stepDataRevision.get());
    assert(
        editor.patternVariationRevision.get() ==
        bank.patternVariationRevision.get()
    );
    assert(editor.patternScaleRevision.get() == bank.patternScaleRevision.get());
    assert(
        editor.patternTimingRevision.get() ==
        bank.patternTimingRevision.get()
    );
    assert(editor.graphRevision.get() == bank.graphRevision.get());
    assert(editor.ccLaneRevision.get() == bank.ccLaneRevision.get());
}

void test_child_macro_and_opt_callers_use_full_payload() {
    constexpr std::array callers{
        ChildMacroCaller::MacroState,
        ChildMacroCaller::MacroOrdinary,
        ChildMacroCaller::OptState,
        ChildMacroCaller::OptOrdinary,
    };

    for (const auto caller : callers) {
        SequencerMacroPropertyHarness h;
        prepareChildGraphAndCc(h, caller);
        g_now_ms = 200U;
        const auto ownersBefore = tx::captureStateInvariant(h.state);
        core::state::sequencer::SequencerHistoryPatternSnapshot before;
        tx::captureMusicalSnapshot(h.state, before);

        {
            // Full Graph+CC owns exactly seven allocations. Seal and commit
            // must leave the max+1 failure armed.
            core::app::testing::ScopedExtmemAllocationFailure failure(8U);
            invokeChildMacroCaller(h, caller);
            tx::assertMaxPlusOneStillArmed(7U);
            assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
            assert(h.state.commitSequencerPatternHistoryCoalescing());
            tx::assertMaxPlusOneStillArmed(7U);
        }

        assert(h.state.sequencerHistory.undoCount() == 1U);
        const auto ownersAfter = tx::captureStateInvariant(h.state);
        assert(ownersAfter.editorGraphOwner == ownersBefore.editorGraphOwner);
        assert(ownersAfter.editorCcOwner == ownersBefore.editorCcOwner);
        assert(ownersAfter.bankGraphOwner != ownersBefore.bankGraphOwner);
        assert(ownersAfter.bankCcOwner != ownersBefore.bankCcOwner);
        assert(ownersAfter.bankGraphOwner != ownersAfter.editorGraphOwner);
        assert(ownersAfter.bankCcOwner != ownersAfter.editorCcOwner);
        assertChildEditorAndBankPayloadMatch(h);

        core::state::sequencer::SequencerHistoryPatternSnapshot after;
        tx::captureMusicalSnapshot(h.state, after);
        assert(!core::state::sequencer::sameMusicalHistorySnapshot(before, after));
        assert(h.state.undoSequencerHistory());
        tx::assertMusicalSnapshot(h.state, before);
        assertChildEditorAndBankPayloadMatch(h);
        assert(h.state.redoSequencerHistory());
        tx::assertMusicalSnapshot(h.state, after);
        assertChildEditorAndBankPayloadMatch(h);
    }

    std::cout
        << "[PASS] child Macro/OPT state and ordinary callers use Full payload\n";
}

void test_macro_encoder_edits_step_in_current_page_and_shows_feedback() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(16);
    h.state.sequencer.page.set(1);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    g_now_ms = 1234;

    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    const uint8_t step = 10;
    assert(h.state.sequencer.pattern.velocity[step] == 127);
    assert(h.state.sequencer.stepInlineFeedback.visible.get());
    assert(h.state.sequencer.stepInlineFeedback.touchedMask.get().test(step));
    assert(h.state.sequencer.stepInlineFeedback.property.get() == StepProperty::VELOCITY);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);

    std::cout << "[PASS] test_macro_encoder_edits_step_in_current_page_and_shows_feedback\n";
}

void test_opt_encoder_does_not_edit_without_step_focus() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(4);
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);

    h.turn(Config::EncoderID::OPT, 0.0f);
    assert(h.state.sequencer.pattern.gate[4] == core::state::sequencer::SequencerState::DEFAULT_GATE_PERCENT);
    assert(!h.state.sequencer.stepInlineFeedback.visible.get());
    assert(!h.state.sequencer.stepInlineFeedback.touchedMask.get().test(4));

    std::cout << "[PASS] test_opt_encoder_does_not_edit_without_step_focus\n";
}

void test_opt_encoder_edits_focused_step_in_step_focus() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.focusedStep.set(4);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    g_now_ms = 2345;

    h.turn(Config::EncoderID::OPT, 1.0f);

    assert(h.state.sequencer.pattern.velocity[4] == 127);
    assert(h.state.sequencer.stepInlineFeedback.visible.get());
    assert(h.state.sequencer.stepInlineFeedback.touchedMask.get().test(4));
    assert(h.state.sequencer.stepInlineFeedback.property.get() == StepProperty::VELOCITY);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.commitSequencerPatternHistoryCoalescing());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);

    std::cout << "[PASS] test_opt_encoder_edits_focused_step_in_step_focus\n";
}

void test_opt_state_caller_seals_pending_edit_and_is_undoable() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8U);
    h.state.sequencer.focusedStep.set(3U);
    h.state.sequencer.stepStatePropertyActive.set(true);
    h.navigationFocus.set(core::state::StructureNavigationFocus::STEP);
    g_now_ms = 3456U;

    h.turn(Config::EncoderID::OPT, 1.0F);

    assert(h.state.sequencer.pattern.isEnabled(3U));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0U);
    assert(h.state.commitSequencerPatternHistoryCoalescing());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1U);
    assert(h.state.undoSequencerHistory());
    assert(!h.state.sequencer.pattern.isEnabled(3U));
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(3U));

    std::cout
        << "[PASS] OPT state caller seals pending edit and is undoable\n";
}

void test_direct_state_edit_coalesces_as_state_history() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.stepStatePropertyActive.set(true);

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(h.state.sequencer.pattern.isEnabled(0));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

    h.advance(700);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(!h.state.sequencer.pattern.isEnabled(0));
    assert(std::strcmp(
        h.state.sequencer.historyFeedback.line2.data(),
        "Step 01 State"
    ) == 0);
    assert(std::strcmp(
        h.state.sequencer.historyFeedback.line3.data(),
        "On -> Off"
    ) == 0);

    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.isEnabled(0));

    std::cout << "[PASS] test_direct_state_edit_coalesces_as_state_history\n";
}

void test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);

    const uint8_t step = 2;
    h.state.sequencer.cycleVariationTelemetry.validMask.setBit(step, true);
    h.state.sequencer.cycleVariationTelemetry.triggeredMask.setBit(step, true);
    h.state.sequencer.cycleVariationTelemetry.resolvedNote[step] = 60;
    h.state.sequencer.variationTelemetryRevision.set(10);

    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    assert(h.state.sequencer.pattern.note[step] == 127);
    assert(!h.state.sequencer.cycleVariationTelemetry.validMask.test(step));
    assert(!h.state.sequencer.cycleVariationTelemetry.triggeredMask.test(step));
    assert(h.state.sequencer.variationTelemetryRevision.get() == 11);

    std::cout << "[PASS] test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step\n";
}

void test_direct_edit_retires_runtime_projection_before_authored_revision() {
    const StepProperty properties[] = {
        StepProperty::NOTE,
        StepProperty::VELOCITY,
        StepProperty::GATE,
        StepProperty::NUDGE,
    };

    for (const auto property : properties) {
        SequencerMacroPropertyHarness h;
        h.state.sequencer.pattern.setContentLength(8);
        h.state.sequencer.activeStepProperty.set(property);

        constexpr uint8_t step = 0;
        h.state.sequencer.cycleVariationTelemetry.validMask.setBit(step, true);
        h.state.sequencer.cycleVariationTelemetry.triggeredMask.setBit(step, true);
        h.state.sequencer.cycleVariationTelemetry.resolvedNote[step] = 12;
        h.state.sequencer.variationTelemetryRevision.set(10);
        oc::state::NotificationQueue::instance().flush();

        uint8_t callbackCount = 0;
        uint8_t callbackOrder[2]{};
        bool authoredRevisionSawCoherentState = false;
        auto telemetrySubscription =
            h.state.sequencer.variationTelemetryRevision.subscribe(
                [&](const uint32_t&) {
                    if (callbackCount < 2U) callbackOrder[callbackCount++] = 1U;
                }
            );
        auto authoredSubscription = h.state.sequencer.pattern.stepDataRevision.subscribe(
            [&](const uint32_t&) {
                if (callbackCount < 2U) callbackOrder[callbackCount++] = 2U;
                authoredRevisionSawCoherentState =
                    !h.state.sequencer.cycleVariationTelemetry.validMask.test(step) &&
                    !h.state.sequencer.cycleVariationTelemetry.triggeredMask.test(step) &&
                    h.state.sequencer.stepInlineFeedback.visible.get() &&
                    h.state.sequencer.stepInlineFeedback.touchedMask.get().test(step) &&
                    h.state.sequencer.stepInlineFeedback.property.get() == property;
            }
        );
        assert(telemetrySubscription.isValid());
        assert(authoredSubscription.isValid());

        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        oc::state::NotificationQueue::instance().flush();

        assert(callbackCount == 2U);
        assert(callbackOrder[0] == 1U);
        assert(callbackOrder[1] == 2U);
        assert(authoredRevisionSawCoherentState);
    }

    std::cout << "[PASS] test_direct_edit_retires_runtime_projection_before_authored_revision\n";
}

void test_follow_scale_pitch_edit_writes_scale_degree_note() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    h.state.sequencer.setStepNoteAt(0, 60);

    oc::note::sequencer::StepSequencerScaleSettings settings{
        .root = 0,
        .type = oc::note::sequencer::StepSequencerScaleType::Major,
        .mode = oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    h.state.sequencerTracks.setProjectScaleSettings(settings);
    h.state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    h.state.sequencer.setPitchEditMode(
        core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE
    );

    const float b4AsScaleDegree = input_utils::indexToNormalized(
        input_utils::scaleDegreeIndexForNote(71, settings),
        input_utils::countScaleNotes(settings)
    );

    h.turn(Config::EncoderID::MACRO_1, b4AsScaleDegree);

    assert(h.state.sequencer.pattern.note[0] == 71);

    std::cout << "[PASS] test_follow_scale_pitch_edit_writes_scale_degree_note\n";
}

void test_macro_property_edits_are_blocked_by_modal_states() {
    {
        SequencerMacroPropertyHarness h;
        h.state.overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.sequencer.structureUi.stepSelection.active.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.state.sequencer.patternQuickControls.selecting.set(true);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] == core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    {
        SequencerMacroPropertyHarness h;
        h.navigationFocus.set(core::state::StructureNavigationFocus::TRACK);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(h.state.sequencer.pattern.note[0] != core::state::sequencer::SequencerState::DEFAULT_NOTE);
    }

    std::cout << "[PASS] test_macro_property_edits_are_blocked_by_modal_states\n";
}

void test_left_bottom_selector_macro_edits_local_variation_range() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    h.state.sequencer.pattern.note[2] = 60;
    h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
    h.press(Config::ButtonID::LEFT_BOTTOM);

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_3, 1.0f);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::NOTE) == 36);
    assert(h.state.sequencer.pattern.note[2] == 60);
    assert(h.state.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive.get());
    assert(h.state.sequencer.stepPropertyInlineSelector.localVariationStepIndex == 2);
    assert(h.state.sequencer.stepInlineFeedback.visible.get());
    assert(h.state.sequencer.stepInlineFeedback.touchedMask.get().test(2));
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.release(Config::ButtonID::LEFT_BOTTOM);

    h.advance(599);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.advance(600);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    if (graph != nullptr) {
        node = graph->stepNode(core::state::sequencer::rootStepNodeId(2));
        assert(
            node == nullptr ||
            core::state::sequencer::nodeLocalVariationRange(*node, StepProperty::NOTE) == 0
        );
    }
    assert(h.state.sequencer.pattern.note[2] == 60);

    std::cout << "[PASS] test_left_bottom_selector_macro_edits_local_variation_range\n";
}

void test_left_bottom_selector_does_not_randomize_probability() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::PROBABILITY);
    h.state.sequencer.pattern.probability[0] = 64;
    h.state.sequencer.stepPropertyInlineSelector.selecting.set(true);
    h.press(Config::ButtonID::LEFT_BOTTOM);

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph == nullptr);
    assert(h.state.sequencer.pattern.probability[0] == 64);
    assert(!h.state.sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive.get());

    h.release(Config::ButtonID::LEFT_BOTTOM);

    std::cout << "[PASS] test_left_bottom_selector_does_not_randomize_probability\n";
}

void test_left_center_quick_controls_do_not_randomize_step() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    h.state.sequencer.pattern.note[0] = 60;
    h.state.sequencer.patternQuickControls.selecting.set(true);
    h.press(Config::ButtonID::LEFT_CENTER);

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    const auto* graph = core::state::sequencer::graphView(h.state.sequencer.pattern);
    assert(graph == nullptr);
    assert(h.state.sequencer.pattern.note[0] == 60);

    h.release(Config::ButtonID::LEFT_CENTER);

    std::cout << "[PASS] test_left_center_quick_controls_do_not_randomize_step\n";
}

void test_macro_property_edits_coalesce_until_idle() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[0] = 0;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 0.25f);
    g_now_ms = 200;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencer.pattern.velocity[0] == 127);

    h.advance(699);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    h.advance(700);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.velocity[0] == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);

    std::cout << "[PASS] test_macro_property_edits_coalesce_until_idle\n";
}

void test_macro_property_step_change_commits_previous_coalesced_edit() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::GATE);
    h.state.sequencer.pattern.gate[0] = 50;
    h.state.sequencer.pattern.gate[1] = 60;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    g_now_ms = 200;
    h.turn(Config::EncoderID::MACRO_2, 0.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);

    h.advance(700);
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 2);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.gate[0] == core::state::sequencer::SequencerState::MAX_GATE_PERCENT);
    assert(h.state.sequencer.pattern.gate[1] == 60);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.gate[0] == 50);

    std::cout << "[PASS] test_macro_property_step_change_commits_previous_coalesced_edit\n";
}

void test_macro_property_track_change_commits_pending_coalesced_edit() {
    SequencerMacroPropertyHarness h;
    h.state.setSharedTrackState(0x0003, 0);
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[0] = 10;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);

    assert(h.state.setSharedTrackState(0x0003, 1));
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencerTracks.track(0).velocity[0] == 127);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerTracks.activeTrackIndex() == 1);
    assert(h.state.sequencerTracks.track(0).velocity[0] == 10);
    assert(h.state.sequencer.pattern.velocity[0] != 10);

    std::cout << "[PASS] test_macro_property_track_change_commits_pending_coalesced_edit\n";
}

void test_macro_property_pending_edit_undoes_with_single_command() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    h.state.sequencer.pattern.note[0] = 60;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencer.pattern.note[0] == 127);

    assert(h.state.undoSequencerHistory());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.pattern.note[0] == 60);
    assert(h.state.sequencerHistory.undoCount() == 0);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(h.state.sequencer.historyFeedback.visible.get());
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "UNDO T01") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "Step 01 Pitch") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "G9 -> C4") == 0);

    std::cout << "[PASS] test_macro_property_pending_edit_undoes_with_single_command\n";
}

void test_macro_property_new_pending_edit_invalidates_redo_on_commit() {
    SequencerMacroPropertyHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.activeStepProperty.set(StepProperty::VELOCITY);
    h.state.sequencer.pattern.velocity[0] = 10;
    h.state.sequencer.pattern.velocity[1] = 20;

    g_now_ms = 100;
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    h.advance(700);
    assert(h.state.sequencerHistory.undoCount() == 1);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(h.state.sequencer.pattern.velocity[0] == 10);

    g_now_ms = 800;
    h.turn(Config::EncoderID::MACRO_2, 1.0f);
    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());

    assert(!h.state.redoSequencerHistory());
    assert(!h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencerHistory.undoCount() == 1);
    assert(h.state.sequencerHistory.redoCount() == 0);
    assert(h.state.sequencer.pattern.velocity[0] == 10);
    assert(h.state.sequencer.pattern.velocity[1] == 127);

    std::cout << "[PASS] test_macro_property_new_pending_edit_invalidates_redo_on_commit\n";
}

}  // namespace

int main() {
    test_all_five_macro_and_opt_callers_reject_fail_one_atomically();
    test_child_macro_and_opt_callers_use_full_payload();
    test_macro_encoder_edits_step_in_current_page_and_shows_feedback();
    test_opt_encoder_does_not_edit_without_step_focus();
    test_opt_encoder_edits_focused_step_in_step_focus();
    test_opt_state_caller_seals_pending_edit_and_is_undoable();
    test_direct_state_edit_coalesces_as_state_history();
    test_macro_encoder_invalidates_stale_runtime_telemetry_for_edited_step();
    test_direct_edit_retires_runtime_projection_before_authored_revision();
    test_follow_scale_pitch_edit_writes_scale_degree_note();
    test_macro_property_edits_are_blocked_by_modal_states();
    test_left_bottom_selector_macro_edits_local_variation_range();
    test_left_bottom_selector_does_not_randomize_probability();
    test_left_center_quick_controls_do_not_randomize_step();
    test_macro_property_edits_coalesce_until_idle();
    test_macro_property_step_change_commits_previous_coalesced_edit();
    test_macro_property_track_change_commits_pending_coalesced_edit();
    test_macro_property_pending_edit_undoes_with_single_command();
    test_macro_property_new_pending_edit_invalidates_redo_on_commit();

    std::cout << "\nAll SequencerMacroPropertyHandler tests passed.\n";
    return 0;
}
