#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

enum class MidiSyncMode : uint8_t {
    MASTER = 0,
    SLAVE = 1,
    AUTO = 2,
};

enum class ClockSourceActive : uint8_t {
    INTERNAL = 0,
    EXTERNAL = 1,
};

struct MidiSyncState {
    // User-facing settings
    oc::state::Signal<MidiSyncMode> mode{MidiSyncMode::AUTO};
    oc::state::Signal<bool> followTransport{true};
    oc::state::Signal<uint16_t> autoFallbackMs{500};
    oc::state::Signal<uint8_t> autoLockClockCount{6};

    // Runtime projection (for UI/diagnostics)
    oc::state::Signal<ClockSourceActive> activeSource{ClockSourceActive::INTERNAL};
    oc::state::Signal<bool> externalClockPresent{false};

    void reset() {
        mode.set(MidiSyncMode::AUTO);
        followTransport.set(true);
        autoFallbackMs.set(500);
        autoLockClockCount.set(6);
        activeSource.set(ClockSourceActive::INTERNAL);
        externalClockPresent.set(false);
    }
};

}  // namespace core::state
