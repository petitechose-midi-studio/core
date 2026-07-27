#include "handler/common/SharedTrackDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

namespace {

FLASHMEM bool setSharedTrackStateFromCoreState(
    void* context,
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->setSharedTrackState(enabledMask, activeTrack);
}

FLASHMEM void publishPreparedSequencerStateFromCoreState(
    void* context,
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    if (context == nullptr) return;

    auto* state = static_cast<core::state::CoreState*>(context);
    state->publishPreparedSequencerTrackState(enabledMask, activeTrack);
}

FLASHMEM void reconcilePreparedMacroTrackTransferFromCoreState(
    void* context,
    uint16_t capturedTrackMask
) {
    if (context == nullptr) return;
    static_cast<core::state::CoreState*>(context)
        ->reconcilePreparedMacroTrackTransfer(capturedTrackMask);
}

}  // namespace

FLASHMEM SharedTrackDomainServices::SharedTrackDomainServices(StateRefs state)
    : SharedTrackDomainServices(state, Operations{}) {}

FLASHMEM SharedTrackDomainServices::SharedTrackDomainServices(StateRefs state, Operations operations)
    : active_track_(&state.activeTrack)
    , enabled_mask_(&state.enabledMask)
    , operations_(operations) {}

FLASHMEM SharedTrackDomainServices SharedTrackDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return SharedTrackDomainServices{
        StateRefs{
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
        },
        Operations{
            &state,
            setSharedTrackStateFromCoreState,
            publishPreparedSequencerStateFromCoreState,
            reconcilePreparedMacroTrackTransferFromCoreState,
        },
    };
}

FLASHMEM uint16_t SharedTrackDomainServices::enabledMask() const {
    return enabled_mask_->get();
}

FLASHMEM uint8_t SharedTrackDomainServices::activeTrack() const {
    return active_track_->get();
}

FLASHMEM bool SharedTrackDomainServices::setState(uint16_t enabledMask, uint8_t activeTrack) const {
    return operations_.setSharedTrackState != nullptr &&
           operations_.setSharedTrackState(operations_.context, enabledMask, activeTrack);
}

FLASHMEM bool SharedTrackDomainServices::canPublishPreparedSequencerState() const {
    return operations_.context != nullptr &&
           operations_.publishPreparedSequencerState != nullptr;
}

FLASHMEM void SharedTrackDomainServices::publishPreparedSequencerState(
    uint16_t enabledMask,
    uint8_t activeTrack
) const {
    operations_.publishPreparedSequencerState(
        operations_.context,
        enabledMask,
        activeTrack
    );
}

FLASHMEM void
SharedTrackDomainServices::reconcilePreparedMacroTrackTransfer(
    uint16_t capturedTrackMask
) const {
    if (operations_.reconcilePreparedMacroTrackTransfer == nullptr) {
        return;
    }
    operations_.reconcilePreparedMacroTrackTransfer(
        operations_.context,
        capturedTrackMask
    );
}

}  // namespace core::handler
