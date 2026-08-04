#include "sequencer/ProjectControlClockPublisher.hpp"

#include <algorithm>

#include <oc/note/clock/ClockConstants.hpp>
#include <oc/realtime/InterruptGuard.hpp>
#include <oc/time/Time.hpp>

namespace core::sequencer {

void ProjectControlClockPublisher::publishLocked(
    uint32_t sequencerTick,
    bool playing,
    uint32_t nowUs,
    uint32_t sequencerTickPeriodUs
) {
    static_assert(
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT %
            oc::note::clock::PPQN == 0U
    );
    constexpr uint32_t PROJECT_TICKS_PER_SEQUENCER_TICK =
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT /
        oc::note::clock::PPQN;

    const bool resynchronized =
        initialized_ && sequencerTick < last_sequencer_tick_;
    if (!initialized_ || sequencerTick != last_sequencer_tick_) {
        tick_started_us_ = nowUs;
    }
    const uint32_t baseTick = static_cast<uint32_t>(
        static_cast<uint64_t>(sequencerTick) *
        PROJECT_TICKS_PER_SEQUENCER_TICK
    );
    uint32_t musicalTick = baseTick;
    uint16_t fractionQ16 = 0U;
    if (playing && sequencerTickPeriodUs > 0U) {
        const uint32_t elapsedUs = std::min<uint32_t>(
            nowUs - tick_started_us_,
            sequencerTickPeriodUs - 1U
        );
        const uint64_t subTickQ16 =
            (static_cast<uint64_t>(elapsedUs) *
             PROJECT_TICKS_PER_SEQUENCER_TICK * 65536ULL) /
            sequencerTickPeriodUs;
        musicalTick = static_cast<uint32_t>(
            musicalTick + static_cast<uint32_t>(subTickQ16 >> 16U)
        );
        fractionQ16 = static_cast<uint16_t>(subTickQ16 & 0xFFFFU);
    }

    const uint32_t nowMs = oc::time::millis();
    const bool transportStarted = playing &&
        (!snapshot_.playing || resynchronized || !initialized_);
    if (transportStarted) {
        ++snapshot_.transportGeneration;
        if (snapshot_.transportGeneration == 0U) {
            snapshot_.transportGeneration = 1U;
        }
        snapshot_.transportStartMusicalTick = musicalTick;
        snapshot_.transportStartMonotonicMs = nowMs;
    }
    snapshot_.musicalTick = musicalTick;
    snapshot_.musicalTickFractionQ16 = fractionQ16;
    snapshot_.monotonicMs = nowMs;
    snapshot_.playing = playing;
    snapshot_.reserved = 0U;
    last_sequencer_tick_ = sequencerTick;
    initialized_ = true;
}

core::state::modulation::ProjectControlTimeSnapshot
ProjectControlClockPublisher::snapshot() const {
    oc::realtime::InterruptGuard lock;
    return snapshot_;
}

void ProjectControlClockPublisher::reset() {
    snapshot_ = {};
    tick_started_us_ = 0U;
    last_sequencer_tick_ = 0U;
    initialized_ = false;
}

}  // namespace core::sequencer
