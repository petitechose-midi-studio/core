#include <cassert>
#include <cmath>
#include <iostream>

#include "../../src/sequencer/ExternalClockEstimator.hpp"

namespace {

void assertNear(float actual, float expected, float epsilon) {
    if (std::fabs(actual - expected) > epsilon) {
        std::cerr << "assertNear failed: actual=" << actual
                  << " expected=" << expected
                  << " eps=" << epsilon << "\n";
        assert(false);
    }
}

void test_estimator_reports_stable_tempo() {
    core::sequencer::ExternalClockEstimator estimator;

    uint32_t nowMs = 100;
    for (int i = 0; i < 16; ++i) {
        const uint32_t previousMs = i == 0 ? 0 : nowMs - 20;
        estimator.recordClock(static_cast<uint64_t>(nowMs) * 1000ULL, nowMs, previousMs);
        nowMs += 20;
    }

    assert(estimator.bpmValid());
    assertNear(estimator.bpmEstimate(), 125.0f, 0.1f);

    std::cout << "[PASS] test_estimator_reports_stable_tempo\n";
}

void test_estimator_tracks_telemetry_and_reset() {
    core::sequencer::ExternalClockEstimator estimator;

    uint32_t nowMs = 100;
    for (int i = 0; i < 7; ++i) {
        const uint32_t previousMs = i == 0 ? 0 : nowMs - 20;
        estimator.recordClock(static_cast<uint64_t>(nowMs) * 1000ULL, nowMs, previousMs);
        nowMs += 20;
    }

    estimator.recordClock(static_cast<uint64_t>(nowMs + 6U) * 1000ULL, nowMs + 6U, nowMs - 20U);

    const auto telemetry = estimator.telemetry();
    assert(telemetry.clockCount == 8);
    assert(telemetry.maxIntervalUs == 26000);
    assert(telemetry.maxHostGapMs == 26);
    assert(telemetry.maxJitterUs == 6000);

    estimator.reset();
    assert(!estimator.bpmValid());
    assert(estimator.telemetry().clockCount == 8);

    const auto taken = estimator.takeTelemetry();
    assert(taken.clockCount == 8);
    assert(estimator.telemetry().clockCount == 0);

    std::cout << "[PASS] test_estimator_tracks_telemetry_and_reset\n";
}

}  // namespace

int main() {
    test_estimator_reports_stable_tempo();
    test_estimator_tracks_telemetry_and_reset();

    std::cout << "All ExternalClockEstimator tests passed\n";
    return 0;
}
