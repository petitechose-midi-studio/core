#include "InternalTransportClock.hpp"

#include <algorithm>
#include <cmath>

namespace core::sequencer {

namespace {
constexpr float DEFAULT_BPM = 120.0f;
constexpr uint32_t MIN_PERIOD_US = 1;
}  // namespace

#ifdef ARDUINO

void InternalTransportClock::setPlaying(bool playing) {
    if (playing_ == playing) {
        return;
    }

    if (!playing) {
        tick_base_ = tick();
        playing_ = false;
        return;
    }

    tick_base_ = 0;
    segment_start_us_ = clock_.micros64();
    playing_ = true;
}

void InternalTransportClock::setBpm(float bpm) {
    if (std::fabs(bpm_ - bpm) < 0.0005f) {
        return;
    }

    if (playing_) {
        tick_base_ = tick();
        segment_start_us_ = clock_.micros64();
    }
    bpm_ = bpm;
}

void InternalTransportClock::reset() {
    bpm_ = DEFAULT_BPM;
    playing_ = false;
    tick_base_ = 0;
    segment_start_us_ = 0;
}

void InternalTransportClock::update(uint32_t /*nowMs*/) {
    // HAL-backed implementation derives phase from monotonic microseconds.
}

uint32_t InternalTransportClock::tick() const {
    if (!playing_) {
        return tick_base_;
    }

    return tick_base_ + segmentTicks_(clock_.micros64());
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

uint32_t InternalTransportClock::segmentTicks_(uint64_t nowUs) const {
    const uint32_t periodUs = tickPeriodUs_();
    if (periodUs == 0) {
        return 0;
    }

    if (nowUs <= segment_start_us_) {
        return 0;
    }

    return static_cast<uint32_t>((nowUs - segment_start_us_) / periodUs);
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
