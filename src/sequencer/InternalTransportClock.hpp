#pragma once

#include <cstdint>

#include "config/PlatformCompat.hpp"

#ifdef ARDUINO
    #include <IntervalTimer.h>
#endif

#include <oc/note/clock/InternalClock.hpp>

namespace core::sequencer {

/**
 * @brief Internal transport clock with a Teensy timer-backed implementation.
 *
 * On Teensy builds, this class owns an IntervalTimer which advances the
 * transport tick independently from the cooperative UI loop cadence.
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
    uint32_t tickPeriodUs_() const;

#ifdef ARDUINO
    static void onTimerThunk_();
    void onTimer_();
    void restartTimer_();
    void stopTimer_();

    static InternalTransportClock* active_instance_;

    IntervalTimer timer_{};
    volatile uint32_t tick_ = 0;
    float bpm_ = 120.0f;
    bool playing_ = false;
    bool timer_running_ = false;
#else
    oc::note::clock::InternalClock fallback_{};
#endif
};

}  // namespace core::sequencer
