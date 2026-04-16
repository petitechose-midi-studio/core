#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/handler/common/SharedTrackDomainServices.hpp"

namespace {

struct SharedTrackMutationRecorder {
    uint16_t enabledMask = 0;
    uint8_t activeTrack = 0;
    bool called = false;
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
        },
    };

    assert(services.activeTrack() == 3);
    assert(services.enabledMask() == 0x0007);
    assert(services.setState(0x0003, 1));
    assert(recorder.called);
    assert(recorder.enabledMask == 0x0003);
    assert(recorder.activeTrack == 1);

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

    std::cout << "[PASS] test_set_state_returns_false_without_operation\n";
}

}  // namespace

int main() {
    test_reads_state_and_uses_explicit_set_state_operation();
    test_set_state_returns_false_without_operation();

    std::cout << "All SharedTrackDomainServices tests passed\n";
    return 0;
}
