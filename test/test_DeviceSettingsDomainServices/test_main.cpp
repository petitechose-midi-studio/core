#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../src/handler/settings/DeviceSettingsDomainServices.hpp"
#include "../support/MemoryStorage.hpp"

namespace {
using test_support::MemoryStorage;

}  // namespace

int main() {
    MemoryStorage storage;
    storage.init();

    core::state::MidiSyncState sync;
    core::state::CoreSettings settings(storage);
    core::handler::DeviceSettingsDomainServices services(
        core::handler::DeviceSettingsDomainServices::StateRefs{
            sync,
            settings,
        }
    );

    assert(services.choiceCount(0) == 3);
    assert(services.choiceCount(1) == 2);
    assert(services.choiceCount(2) == 7);
    assert(services.choiceCount(3) == 8);
    assert(services.currentChoiceIndex(0) == 2);
    assert(services.currentChoiceIndex(1) == 1);
    assert(services.currentChoiceIndex(2) == 2);
    assert(services.currentChoiceIndex(3) == 4);

    services.applyChoice(0, 1);
    assert(sync.mode.get() == core::state::MidiSyncMode::SLAVE);
    assert(!storage.isDirty());

    services.applyChoice(1, 0);
    assert(!sync.followTransport.get());
    assert(!storage.isDirty());

    services.applyChoice(2, 5);
    assert(sync.autoFallbackMs.get() == 1500);
    assert(!storage.isDirty());

    services.applyChoice(3, 7);
    assert(sync.autoLockClockCount.get() == 24);
    assert(!storage.isDirty());

    return 0;
}
