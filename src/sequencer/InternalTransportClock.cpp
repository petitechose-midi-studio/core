#include "InternalTransportClock.hpp"

#include <algorithm>
#include <cmath>

namespace core::sequencer {

namespace {
constexpr float DEFAULT_BPM = 120.0f;
constexpr uint8_t TIMER_PRIORITY = 32;
constexpr uint32_t MIN_PERIOD_US = 1;
}  // namespace

#ifdef ARDUINO

InternalTransportClock* InternalTransportClock::active_instance_ = nullptr;

void InternalTransportClock::setPlaying(bool playing) {
    if (playing_ == playing) {
        return;
    }

    playing_ = playing;
    if (!playing_) {
        stopTimer_();
        return;
    }

    noInterrupts();
    tick_ = 0;
    interrupts();
    restartTimer_();
}

void InternalTransportClock::setBpm(float bpm) {
    if (std::fabs(bpm_ - bpm) < 0.0005f) {
        return;
    }

    bpm_ = bpm;
    if (playing_) {
        restartTimer_();
    }
}

void InternalTransportClock::reset() {
    stopTimer_();
    bpm_ = DEFAULT_BPM;
    playing_ = false;
    noInterrupts();
    tick_ = 0;
    interrupts();
}

void InternalTransportClock::update(uint32_t /*nowMs*/) {
    // Timer-backed implementation does not derive phase from cooperative updates.
}

uint32_t InternalTransportClock::tick() const {
    return tick_;
}

float InternalTransportClock::bpm() const {
    return bpm_;
}

bool InternalTransportClock::isPlaying() const {
    return playing_;
}

uint32_t InternalTransportClock::tickPeriodUs_() const {
    const float clampedBpm = std::max(bpm_, 0.0f);
    if (!(clampedBpm > 0.0f)) {
        return 0;
    }

    constexpr float ppqn = static_cast<float>(oc::note::clock::PPQN);
    const float periodUs = 60000000.0f / (clampedBpm * ppqn);
    return std::max(static_cast<uint32_t>(periodUs + 0.5f), MIN_PERIOD_US);
}

void InternalTransportClock::onTimerThunk_() {
    if (active_instance_) {
        active_instance_->onTimer_();
    }
}

void InternalTransportClock::onTimer_() {
    tick_ += 1;
}

void InternalTransportClock::restartTimer_() {
    const uint32_t periodUs = tickPeriodUs_();
    if (periodUs == 0) {
        stopTimer_();
        return;
    }

    active_instance_ = this;
    if (!timer_running_) {
        timer_.priority(TIMER_PRIORITY);
        timer_running_ = timer_.begin(onTimerThunk_, periodUs);
        return;
    }

    timer_.update(periodUs);
}

void InternalTransportClock::stopTimer_() {
    if (!timer_running_) {
        return;
    }

    timer_.end();
    timer_running_ = false;
    if (active_instance_ == this) {
        active_instance_ = nullptr;
    }
}

#else

void InternalTransportClock::setPlaying(bool playing) {
    fallback_.setPlaying(playing);
}

void InternalTransportClock::setBpm(float bpm) {
    if (std::fabs(fallback_.bpm() - bpm) < 0.0005f) {
        return;
    }
    fallback_.setBpm(bpm);
}

void InternalTransportClock::reset() {
    fallback_.reset();
}

void InternalTransportClock::update(uint32_t nowMs) {
    fallback_.update(nowMs);
}

uint32_t InternalTransportClock::tick() const {
    return fallback_.tick();
}

float InternalTransportClock::bpm() const {
    return fallback_.bpm();
}

bool InternalTransportClock::isPlaying() const {
    return fallback_.isPlaying();
}

uint32_t InternalTransportClock::tickPeriodUs_() const {
    return 0;
}

#endif

}  // namespace core::sequencer
