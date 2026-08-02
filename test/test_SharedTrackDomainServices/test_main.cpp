#include <cassert>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/CoreState.hpp"
#include "support/CoreStorages.hpp"

namespace {

struct SharedTrackMutationRecorder {
    uint16_t enabledMask = 0;
    uint8_t activeTrack = 0;
    bool called = false;
    bool preparedCalled = false;
    bool presentationCalled = false;
    core::handler::PreparedTrackPresentationKind presentationKind =
        core::handler::PreparedTrackPresentationKind::MacroTrackTransfer;
    uint16_t presentationMask = 0U;
};

struct CoreHarness {
    test_support::CoreStorages storages;
    core::state::CoreState state;

    CoreHarness() : state(storages.settings) {}
};

bool recordSharedTrackState(void* context, uint16_t enabledMask, uint8_t activeTrack) {
    auto* recorder = static_cast<SharedTrackMutationRecorder*>(context);
    if (recorder == nullptr) {
        return false;
    }

    recorder->enabledMask = enabledMask;
    recorder->activeTrack = activeTrack;
    recorder->called = true;
    return true;
}

void recordPreparedSequencerState(
    void* context,
    uint16_t enabledMask,
    uint8_t activeTrack
) noexcept {
    auto* recorder = static_cast<SharedTrackMutationRecorder*>(context);
    if (recorder == nullptr) return;
    recorder->enabledMask = enabledMask;
    recorder->activeTrack = activeTrack;
    recorder->preparedCalled = true;
}

void recordPreparedTrackPresentation(
    void* context,
    core::handler::PreparedTrackPresentationKind kind,
    uint16_t capturedTrackMask
) noexcept {
    auto* recorder = static_cast<SharedTrackMutationRecorder*>(context);
    if (recorder == nullptr) return;
    recorder->presentationCalled = true;
    recorder->presentationKind = kind;
    recorder->presentationMask = capturedTrackMask;
}

void assertFloatNear(float actual, float expected) {
    assert(std::fabs(actual - expected) < 0.0001f);
}

void test_reads_state_and_uses_explicit_set_state_operation() {
    oc::state::Signal<uint8_t, 8> activeTrack{3};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0007};
    SharedTrackMutationRecorder recorder;

    core::handler::SharedTrackDomainServices services{
        core::handler::SharedTrackDomainServices::StateRefs{
            activeTrack,
            enabledMask,
        },
        core::handler::SharedTrackDomainServices::Operations{
            &recorder,
            recordSharedTrackState,
            recordPreparedSequencerState,
        },
    };

    assert(services.activeTrack() == 3);
    assert(services.enabledMask() == 0x0007);
    assert(services.setState(0x0003, 1));
    assert(recorder.called);
    assert(recorder.enabledMask == 0x0003);
    assert(recorder.activeTrack == 1);
    assert(services.canPublishPreparedSequencerState());
    services.publishPreparedSequencerState(0x0018, 4);
    assert(recorder.preparedCalled);
    assert(recorder.enabledMask == 0x0018);
    assert(recorder.activeTrack == 4);
    assert(!services.canReconcilePreparedSequencerActiveTrackPresentation());

    std::cout << "[PASS] test_reads_state_and_uses_explicit_set_state_operation\n";
}

void test_set_state_returns_false_without_operation() {
    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};

    core::handler::SharedTrackDomainServices services{
        core::handler::SharedTrackDomainServices::StateRefs{
            activeTrack,
            enabledMask,
        }
    };

    assert(!services.setState(0x0003, 1));
    assert(!services.canPublishPreparedSequencerState());
    assert(!services.canReconcilePreparedSequencerActiveTrackPresentation());

    std::cout << "[PASS] test_set_state_returns_false_without_operation\n";
}

void test_prepared_sequencer_presentation_capability_is_explicit() {
    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    SharedTrackMutationRecorder recorder;

    core::handler::SharedTrackDomainServices services{
        core::handler::SharedTrackDomainServices::StateRefs{
            activeTrack,
            enabledMask,
        },
        core::handler::SharedTrackDomainServices::Operations{
            &recorder,
            nullptr,
            nullptr,
            recordPreparedTrackPresentation,
            nullptr,
        },
    };

    assert(services.canReconcilePreparedSequencerActiveTrackPresentation());
    services.reconcilePreparedSequencerActiveTrackPresentation();
    assert(recorder.presentationCalled);
    assert(recorder.presentationKind ==
           core::handler::PreparedTrackPresentationKind::SequencerActiveTrack);
    assert(recorder.presentationMask == 0U);

    recorder.presentationCalled = false;
    services.reconcilePreparedMacroTrackTransfer(0xA55AU);
    assert(recorder.presentationCalled);
    assert(recorder.presentationKind ==
           core::handler::PreparedTrackPresentationKind::MacroTrackTransfer);
    assert(recorder.presentationMask == 0xA55AU);

    std::cout
        << "[PASS] prepared Sequencer presentation capability is explicit\n";
}

void test_core_prepared_sequencer_presentation_projects_base_and_manual_only() {
    auto harness = std::make_unique<CoreHarness>();
    auto& state = harness->state;
    auto services = core::handler::SharedTrackDomainServices::fromCoreState(
        state
    );
    assert(services.canReconcilePreparedSequencerActiveTrackPresentation());

    constexpr std::array<float, core::state::macro::MACRO_COUNT> baseValues{
        0.11f,
        0.22f,
        0.33f,
        0.44f,
        0.55f,
        0.66f,
        0.77f,
        0.88f,
    };
    auto& activePage = state.pages.activePageData();
    for (uint8_t macro = 0U; macro < core::state::macro::MACRO_COUNT; ++macro) {
        activePage.values[macro] = baseValues[macro];
        state.macros.slots[macro].value.set(0.99f);
    }

    const uint8_t activeTrack = state.pages.currentActiveTrack();
    const uint8_t activePageIndex = state.pages.currentActivePage();
    const auto manualOne = core::state::macro::MacroAutomationSlotAddress{
        .track = activeTrack,
        .page = activePageIndex,
        .macro = 1U,
    };
    const auto manualSix = core::state::macro::MacroAutomationSlotAddress{
        .track = activeTrack,
        .page = activePageIndex,
        .macro = 6U,
    };
    const auto unrelatedManual =
        core::state::macro::MacroAutomationSlotAddress{
            .track = 1U,
            .page = 0U,
            .macro = 3U,
        };
    using ActivateStatus =
        core::state::macro::MacroManualOverrideState::ActivateStatus;
    assert(state.macroUi.manualOverrides.activate(manualOne, 0.81f) ==
           ActivateStatus::ACTIVATED);
    assert(state.macroUi.manualOverrides.activate(manualSix, 0.16f) ==
           ActivateStatus::ACTIVATED);
    assert(state.macroUi.manualOverrides.activate(unrelatedManual, 0.93f) ==
           ActivateStatus::ACTIVATED);
    state.macroUi.automationManualOverrideMask.set(0x00FFU);

    state.sequencer.stepInlineFeedback.show(
        3U,
        core::state::sequencer::StepProperty::VELOCITY,
        1000U
    );
    state.sequencer.patternQuickControls.selecting.set(true);
    state.sequencer.contextSelector.visible = true;
    state.sequencer.contextSelector.previewFocus =
        core::state::StructureNavigationFocus::TRACK;

    const uint32_t manualRevision = state.macroUi.manualOverrides.revision;
    const uint32_t rejectedActivationCount =
        state.macroUi.manualOverrides.rejectedActivationCount;
    const uint32_t controlAuthoredRevision =
        state.pages.control.authoredRevision;
    const uint32_t configRevision = state.configRevision.get();
    const uint32_t automationEditRevision =
        state.macroUi.automationEditRevision.get();
    const uint32_t runtimeProjectionRevision =
        state.macroUi.runtimeProjectionRevision.get();
    const uint32_t runtimeOwnerRevision =
        state.macroRuntimeOwnerRevision.get();
    const uint8_t projectNavigationRevision =
        state.projectNavigation.contentRevision.get();

    services.reconcilePreparedSequencerActiveTrackPresentation();

    for (uint8_t macro = 0U; macro < core::state::macro::MACRO_COUNT; ++macro) {
        const float expected = macro == 1U ? 0.81f
                             : macro == 6U ? 0.16f
                                           : baseValues[macro];
        assertFloatNear(state.macros.slots[macro].value.get(), expected);
    }
    assert(state.macroUi.automationManualOverrideMask.get() == 0x0042U);

    assert(state.macroUi.manualOverrides.revision == manualRevision);
    assert(state.macroUi.manualOverrides.rejectedActivationCount ==
           rejectedActivationCount);
    assert(state.pages.control.authoredRevision == controlAuthoredRevision);
    assert(state.configRevision.get() == configRevision);
    assert(state.macroUi.automationEditRevision.get() == automationEditRevision);
    assert(state.macroUi.runtimeProjectionRevision.get() ==
           runtimeProjectionRevision);
    assert(state.macroRuntimeOwnerRevision.get() == runtimeOwnerRevision);
    assert(state.projectNavigation.contentRevision.get() ==
           projectNavigationRevision);

    assert(state.sequencer.stepInlineFeedback.visible.get());
    assert(state.sequencer.stepInlineFeedback.touchedMask.get().test(3U));
    assert(state.sequencer.patternQuickControls.selecting.get());
    assert(state.sequencer.contextSelector.visible);
    assert(state.sequencer.contextSelector.previewFocus ==
           core::state::StructureNavigationFocus::TRACK);

    std::cout
        << "[PASS] Core prepared Sequencer presentation projects Base + Manual only\n";
}

template <typename Mutate>
void assertCoreCheckpointRejects(Mutate mutate) {
    auto harness = std::make_unique<CoreHarness>();
    auto services = core::handler::SharedTrackDomainServices::fromCoreState(
        harness->state
    );
    core::handler::PreparedTrackStructureSettlementCheckpoint checkpoint{};
    assert(services.capturePreparedTrackStructureSettlementCheckpoint(
        checkpoint
    ));
    assert(services.preparedTrackStructureSettlementCheckpointMatches(
        checkpoint
    ));
    mutate(harness->state);
    assert(!services.preparedTrackStructureSettlementCheckpointMatches(
        checkpoint
    ));
}

void test_core_settlement_checkpoint_covers_every_reconciled_domain() {
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        ++state.macroUi.manualOverrides.revision;
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        ++state.macroUi.manualOverrides.rejectedActivationCount;
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        ++state.pages.control.authoredRevision;
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        state.configRevision.set(state.configRevision.get() + 1U);
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        state.macroUi.automationEditRevision.set(
            state.macroUi.automationEditRevision.get() + 1U
        );
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        state.macroUi.runtimeProjectionRevision.set(
            state.macroUi.runtimeProjectionRevision.get() + 1U
        );
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        state.macroUi.automationManualOverrideMask.set(0x0001U);
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        state.projectNavigation.contentRevision.set(
            static_cast<uint8_t>(
                state.projectNavigation.contentRevision.get() + 1U
            )
        );
    });
    assertCoreCheckpointRejects([](core::state::CoreState& state) {
        // Fingerprint coverage is independent from contentRevision.
        state.projectNavigation.focusedRow.set(1U);
    });

    std::cout
        << "[PASS] Core settlement checkpoint covers every reconciled domain\n";
}

}  // namespace

int main() {
    test_reads_state_and_uses_explicit_set_state_operation();
    test_set_state_returns_false_without_operation();
    test_prepared_sequencer_presentation_capability_is_explicit();
    test_core_prepared_sequencer_presentation_projects_base_and_manual_only();
    test_core_settlement_checkpoint_covers_every_reconciled_domain();

    std::cout << "All SharedTrackDomainServices tests passed\n";
    return 0;
}
