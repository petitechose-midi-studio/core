#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {
struct CoreState;
}

namespace core::handler {

class SharedTrackDomainServices {
public:
    struct StateRefs {
        oc::state::Signal<uint8_t, 8>& activeTrack;
        oc::state::Signal<uint16_t, 16>& enabledMask;
    };

    struct Hooks {
        core::state::CoreState* coreState = nullptr;
    };

    SharedTrackDomainServices(StateRefs state, Hooks hooks);
    static SharedTrackDomainServices fromCoreState(core::state::CoreState& state);

    uint16_t enabledMask() const;
    uint8_t activeTrack() const;
    bool setState(uint16_t enabledMask, uint8_t activeTrack) const;

private:
    oc::state::Signal<uint8_t, 8>* active_track_ = nullptr;
    oc::state::Signal<uint16_t, 16>* enabled_mask_ = nullptr;
    Hooks hooks_{};
};

}  // namespace core::handler
