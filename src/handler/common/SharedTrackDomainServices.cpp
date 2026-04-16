#include "handler/common/SharedTrackDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

FLASHMEM SharedTrackDomainServices::SharedTrackDomainServices(StateRefs state, Hooks hooks)
    : active_track_(&state.activeTrack)
    , enabled_mask_(&state.enabledMask)
    , hooks_(hooks) {}

FLASHMEM SharedTrackDomainServices SharedTrackDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return SharedTrackDomainServices{
        StateRefs{
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
        },
        Hooks{&state},
    };
}

FLASHMEM uint16_t SharedTrackDomainServices::enabledMask() const {
    return enabled_mask_->get();
}

FLASHMEM uint8_t SharedTrackDomainServices::activeTrack() const {
    return active_track_->get();
}

FLASHMEM bool SharedTrackDomainServices::setState(uint16_t enabledMask, uint8_t activeTrack) const {
    return hooks_.coreState != nullptr &&
           hooks_.coreState->setSharedTrackState(enabledMask, activeTrack);
}

}  // namespace core::handler
