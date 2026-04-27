#pragma once

#include <array>
#include <cstdint>

namespace core::sequencer {

class ExternalClockEstimator {
public:
    struct Telemetry {
        uint32_t clockCount = 0;
        uint32_t maxIntervalUs = 0;
        uint32_t maxHostGapMs = 0;
        uint32_t maxJitterUs = 0;
    };

    void reset();
    void recordClock(uint64_t timestampUs, uint32_t hostNowMs, uint32_t previousHostClockMs);

    bool bpmValid() const { return bpm_valid_; }
    float bpmEstimate() const { return bpm_estimate_; }

    Telemetry telemetry() const { return telemetry_; }
    Telemetry takeTelemetry();

private:
    void pushIntervalUs_(uint32_t intervalUs);
    uint32_t meanIntervalUs_() const;
    float estimateTempoFromIntervals_() const;

    uint64_t last_clock_us_ = 0;
    std::array<uint32_t, 24> interval_us_{};
    uint8_t interval_count_ = 0;
    uint8_t interval_write_idx_ = 0;
    float bpm_estimate_ = 120.0f;
    bool bpm_valid_ = false;
    Telemetry telemetry_{};
};

}  // namespace core::sequencer
