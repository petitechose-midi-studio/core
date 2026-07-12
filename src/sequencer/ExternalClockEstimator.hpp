#pragma once

#include <array>
#include <cstdint>

namespace core::sequencer {

/**
 * Estimates external MIDI clock tempo and timing quality.
 *
 * The estimator consumes timestamped MIDI clock pulses, filters interval
 * samples into a BPM estimate, and does not decide whether the external source
 * is authoritative.
 */
class ExternalClockEstimator {
public:
    void reset();
    void recordClock(uint64_t timestampUs);

    bool bpmValid() const { return bpm_valid_; }
    float bpmEstimate() const { return bpm_estimate_; }

private:
    void pushIntervalUs_(uint32_t intervalUs);
    float estimateTempoFromIntervals_() const;

    uint64_t last_clock_us_ = 0;
    std::array<uint32_t, 24> interval_us_{};
    uint8_t interval_count_ = 0;
    uint8_t interval_write_idx_ = 0;
    float bpm_estimate_ = 120.0f;
    bool bpm_valid_ = false;
};

}  // namespace core::sequencer
