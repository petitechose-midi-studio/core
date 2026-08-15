#pragma once

#include <cstdint>

#include "config/PlatformCompat.hpp"

#ifdef ARDUINO
    #include <oc/hal/teensy/HighResolutionClock.hpp>
#endif

#include <oc/note/clock/InternalClock.hpp>

namespace core::sequencer {

/**
 * @brief Internal transport clock with a Teensy high-resolution time source.
 *
 * On Teensy builds, the musical transport still lives in core, but the raw
 * monotonic time source comes from the Teensy HAL. This keeps hardware details
 * out of the domain logic while avoiding a transport tick that depends on a
 * periodic ISR firing on time.
 *
 * On non-Arduino/native builds, it falls back to the existing loop-driven
 * `oc::note::clock::InternalClock` so host builds keep their current behavior.
 */
class InternalTransportClock {
public:
    void setPlaying(bool playing);
    void setBpm(float bpm);
    void reset();
    void update(uint32_t nowMs);

    uint32_t tick() const;
    float bpm() const;
    bool isPlaying() const;

private:
#ifdef ARDUINO
    uint32_t tickPeriodUs_() const;
    uint32_t segmentTicks_(uint64_t nowUs) const;

    mutable oc::hal::teensy::HighResolutionClock clock_{};
    uint64_t segment_start_us_ = 0;
    uint32_t tick_base_ = 0;
    float bpm_ = 120.0f;
    bool playing_ = false;
#else
    oc::note::clock::InternalClock fallback_{};
#endif
};

}  // namespace core::sequencer
