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
        estimator.recordClock(static_cast<uint64_t>(nowMs) * 1000ULL);
        nowMs += 20;
    }

    assert(estimator.bpmValid());
    assertNear(estimator.bpmEstimate(), 125.0f, 0.1f);
    assert(estimator.tickPeriodUsEstimate() == 20'000U);

    std::cout << "[PASS] test_estimator_reports_stable_tempo\n";
}

void test_reset_discards_the_previous_tempo_window() {
    core::sequencer::ExternalClockEstimator estimator;

    uint32_t nowMs = 100;
    for (int i = 0; i < 16; ++i) {
        estimator.recordClock(static_cast<uint64_t>(nowMs) * 1000ULL);
        nowMs += 20;
    }

    assert(estimator.bpmValid());
    estimator.reset();
    assert(!estimator.bpmValid());
    assert(estimator.tickPeriodUsEstimate() == 0U);

    for (int i = 0; i < 16; ++i) {
        estimator.recordClock(static_cast<uint64_t>(nowMs) * 1000ULL);
        nowMs += 25;
    }

    assert(estimator.bpmValid());
    assertNear(estimator.bpmEstimate(), 100.0f, 0.1f);
    assert(estimator.tickPeriodUsEstimate() == 25'000U);

    std::cout << "[PASS] test_reset_discards_the_previous_tempo_window\n";
}

void test_period_is_available_during_estimator_warmup() {
    core::sequencer::ExternalClockEstimator estimator;

    estimator.recordClock(10'000U);
    assert(estimator.tickPeriodUsEstimate() == 0U);

    estimator.recordClock(30'000U);
    assert(!estimator.bpmValid());
    assert(estimator.tickPeriodUsEstimate() == 20'000U);

    estimator.recordClock(31'000U);  // Invalid 1 ms interval is ignored.
    assert(estimator.tickPeriodUsEstimate() == 20'000U);

    estimator.reset();
    assert(estimator.tickPeriodUsEstimate() == 0U);

    std::cout << "[PASS] test_period_is_available_during_estimator_warmup\n";
}

}  // namespace

int main() {
    test_estimator_reports_stable_tempo();
    test_reset_discards_the_previous_tempo_window();
    test_period_is_available_during_estimator_warmup();

    std::cout << "All ExternalClockEstimator tests passed\n";
    return 0;
}
