#include <cassert>
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

    std::cout << "[PASS] test_set_state_returns_false_without_operation\n";
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
    test_core_settlement_checkpoint_covers_every_reconciled_domain();

    std::cout << "All SharedTrackDomainServices tests passed\n";
    return 0;
}
