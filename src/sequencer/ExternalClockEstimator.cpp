#include "ExternalClockEstimator.hpp"

#include <algorithm>
#include <cmath>

namespace core::sequencer {

namespace {
constexpr uint32_t MIN_CLOCK_INTERVAL_US = 2500;
constexpr uint32_t MAX_CLOCK_INTERVAL_US = 250000;
constexpr uint8_t MIN_INTERVAL_SAMPLES = 6;
constexpr float MIDI_CLOCK_PPQN = 24.0f;
constexpr float MICROSECONDS_PER_MINUTE = 60000000.0f;
constexpr float MIN_BPM = 20.0f;
constexpr float MAX_BPM = 666.0f;
constexpr float TEMPO_ALPHA_STABLE = 0.18f;
constexpr float TEMPO_ALPHA_MEDIUM = 0.35f;
constexpr float TEMPO_ALPHA_FAST = 0.55f;
}  // namespace

void ExternalClockEstimator::reset() {
    last_clock_us_ = 0;
    interval_count_ = 0;
    interval_write_idx_ = 0;
    bpm_valid_ = false;
}

void ExternalClockEstimator::recordClock(uint64_t timestampUs) {
    if (last_clock_us_ > 0 && timestampUs > last_clock_us_) {
        const uint64_t deltaUs64 = timestampUs - last_clock_us_;
        const uint32_t intervalUs = deltaUs64 > UINT32_MAX
                                        ? UINT32_MAX
                                        : static_cast<uint32_t>(deltaUs64);

        if (intervalUs >= MIN_CLOCK_INTERVAL_US && intervalUs <= MAX_CLOCK_INTERVAL_US) {
            pushIntervalUs_(intervalUs);

            const float sampleBpm = estimateTempoFromIntervals_();
            if (sampleBpm >= MIN_BPM && sampleBpm <= MAX_BPM) {
                if (!bpm_valid_) {
                    bpm_estimate_ = sampleBpm;
                    bpm_valid_ = true;
                } else {
                    const float error = std::fabs(sampleBpm - bpm_estimate_);
                    float alpha = TEMPO_ALPHA_STABLE;
                    if (error > 8.0f) {
                        alpha = TEMPO_ALPHA_FAST;
                    } else if (error > 2.5f) {
                        alpha = TEMPO_ALPHA_MEDIUM;
                    }

                    bpm_estimate_ = bpm_estimate_ * (1.0f - alpha) + sampleBpm * alpha;
                }
            }
        }
    }

    last_clock_us_ = timestampUs;
}

uint32_t ExternalClockEstimator::tickPeriodUsEstimate() const {
    if (bpm_valid_) {
        const float periodUs =
            MICROSECONDS_PER_MINUTE / (bpm_estimate_ * MIDI_CLOCK_PPQN);
        return std::max<uint32_t>(
            1U,
            static_cast<uint32_t>(periodUs + 0.5f)
        );
    }

    if (interval_count_ == 0U) {
        return 0U;
    }

    const uint8_t latestIndex = interval_write_idx_ == 0U
        ? static_cast<uint8_t>(interval_us_.size() - 1U)
        : static_cast<uint8_t>(interval_write_idx_ - 1U);
    return interval_us_[latestIndex];
}

void ExternalClockEstimator::pushIntervalUs_(uint32_t intervalUs) {
    interval_us_[interval_write_idx_] = intervalUs;

    const uint8_t next = static_cast<uint8_t>(interval_write_idx_ + 1);
    interval_write_idx_ = static_cast<uint8_t>(next % interval_us_.size());

    if (interval_count_ < interval_us_.size()) {
        interval_count_ += 1;
    }
}

float ExternalClockEstimator::estimateTempoFromIntervals_() const {
    if (interval_count_ < MIN_INTERVAL_SAMPLES) {
        return 0.0f;
    }

    std::array<uint32_t, 24> sorted{};
    for (uint8_t i = 0; i < interval_count_; ++i) {
        sorted[i] = interval_us_[i];
    }

    std::sort(sorted.begin(), sorted.begin() + interval_count_);

    uint8_t trim = 0;
    if (interval_count_ >= 10) {
        trim = static_cast<uint8_t>(interval_count_ / 5);
    }

    if (trim * 2 >= interval_count_) {
        trim = 0;
    }

    const uint8_t start = trim;
    const uint8_t end = static_cast<uint8_t>(interval_count_ - trim);
    if (end <= start) {
        return 0.0f;
    }

    uint64_t sumUs = 0;
    for (uint8_t i = start; i < end; ++i) {
        sumUs += sorted[i];
    }

    const uint8_t used = static_cast<uint8_t>(end - start);
    if (used == 0) {
        return 0.0f;
    }

    const float meanUs = static_cast<float>(sumUs) / static_cast<float>(used);
    if (meanUs <= 0.0f) {
        return 0.0f;
    }

    return MICROSECONDS_PER_MINUTE / (meanUs * MIDI_CLOCK_PPQN);
}

}  // namespace core::sequencer
