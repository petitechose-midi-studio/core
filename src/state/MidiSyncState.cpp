#include "state/MidiSyncState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM MidiSyncState::~MidiSyncState() = default;

FLASHMEM void MidiSyncState::reset() {
    mode.set(MidiSyncMode::AUTO);
    followTransport.set(true);
    autoFallbackMs.set(500);
    autoLockClockCount.set(6);
    activeSource.set(ClockSourceActive::INTERNAL);
    externalClockPresent.set(false);
}

}  // namespace core::state
