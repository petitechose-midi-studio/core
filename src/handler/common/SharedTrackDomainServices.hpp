#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * Shared-track mutation facade for macro and sequencer handlers.
 *
 * Read access comes from shared track signals. Production writes go through a
 * CoreState operation so macro, sequencer, settings, and persistence stay in sync.
 */
class SharedTrackDomainServices {
public:
    struct StateRefs {
        oc::state::Signal<uint8_t, 8>& activeTrack;
        oc::state::Signal<uint16_t, 16>& enabledMask;
    };

    using SetSharedTrackStateFn = bool (*)(void* context,
                                           uint16_t enabledMask,
                                           uint8_t activeTrack);
    using PublishPreparedSequencerStateFn = void (*)(
        void* context,
        uint16_t enabledMask,
        uint8_t activeTrack
    );

    struct Operations {
        void* context = nullptr;
        SetSharedTrackStateFn setSharedTrackState = nullptr;
        PublishPreparedSequencerStateFn publishPreparedSequencerState = nullptr;
    };

    explicit SharedTrackDomainServices(StateRefs state);
    SharedTrackDomainServices(StateRefs state, Operations operations);
    static SharedTrackDomainServices fromCoreState(core::state::CoreState& state);

    uint16_t enabledMask() const;
    uint8_t activeTrack() const;
    bool setState(uint16_t enabledMask, uint8_t activeTrack) const;
    bool canPublishPreparedSequencerState() const;
    void publishPreparedSequencerState(uint16_t enabledMask, uint8_t activeTrack) const;

private:
    oc::state::Signal<uint8_t, 8>* active_track_ = nullptr;
    oc::state::Signal<uint16_t, 16>* enabled_mask_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler
