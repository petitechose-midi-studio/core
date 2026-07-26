#include <Arduino.h>
#include <imxrt.h>

#include "validation/project/ProjectModulationBenchmark.hpp"

namespace validation = core::validation::project;

namespace {

EXTMEM validation::ProjectModulationBenchmarkWorkspace benchmarkWorkspace;
validation::ProjectModulationBenchmarkResult lfoResult{};
validation::ProjectModulationBenchmarkResult recordedShapeResult{};
validation::ProjectModulationBenchmarkResult adsrResult{};
validation::ProjectModulationBenchmarkResult dahdsrSharedTrackResult{};
bool benchmarkComplete = false;

bool allCasesWithinBudget() {
    return lfoResult.withinBudget() && recordedShapeResult.withinBudget() &&
        adsrResult.withinBudget() && dahdsrSharedTrackResult.withinBudget();
}

void printResult(const validation::ProjectModulationBenchmarkResult& result) {
    Serial.printf(
        "[modulation-benchmark] case=%s prepared=%u evaluated=%u "
        "sources=%u bindings=%u destinations=%u iterations=%lu "
        "trigger_events=%u trigger_routes=%u trigger_tests_per_frame=%lu "
        "avg_us=%lu avg_budget_us=%lu max_us=%lu max_budget_us=%lu "
        "checksum=%lu result=%s\n",
        validation::projectModulationBenchmarkCaseLabel(result.benchmarkCase),
        result.prepared ? 1U : 0U,
        result.evaluated ? 1U : 0U,
        static_cast<unsigned>(result.sourceCount),
        static_cast<unsigned>(result.bindingCount),
        static_cast<unsigned>(result.destinationCount),
        static_cast<unsigned long>(result.iterations),
        static_cast<unsigned>(result.triggerEventCount),
        static_cast<unsigned>(result.triggerRouteCount),
        static_cast<unsigned long>(result.triggerTestsPerFrame),
        static_cast<unsigned long>(result.averageUs),
        static_cast<unsigned long>(
            validation::PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US
        ),
        static_cast<unsigned long>(result.maximumUs),
        static_cast<unsigned long>(
            validation::PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US
        ),
        static_cast<unsigned long>(result.checksum),
        result.withinBudget() ? "PASS" : "FAIL"
    );
}

validation::ProjectModulationBenchmarkResult runCase(
    validation::ProjectModulationBenchmarkCase benchmarkCase
) {
    validation::ProjectModulationBenchmarkResult result{};
    result.benchmarkCase = benchmarkCase;
    result.prepared = validation::prepareProjectModulationBenchmark(
        benchmarkWorkspace,
        benchmarkCase
    );
    if (!result.prepared) return result;
    return validation::runProjectModulationBenchmark(
        benchmarkWorkspace,
        benchmarkCase
    );
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.println("[modulation-benchmark] start");
    lfoResult = runCase(validation::ProjectModulationBenchmarkCase::LFO);
    printResult(lfoResult);
    recordedShapeResult = runCase(
        validation::ProjectModulationBenchmarkCase::RECORDED_SHAPE
    );
    printResult(recordedShapeResult);
    adsrResult = runCase(validation::ProjectModulationBenchmarkCase::ADSR);
    printResult(adsrResult);
    dahdsrSharedTrackResult = runCase(
        validation::ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK
    );
    printResult(dahdsrSharedTrackResult);
    benchmarkComplete = true;
    Serial.printf(
        "[modulation-benchmark] done result=%s\n",
        allCasesWithinBudget() ? "PASS" : "FAIL"
    );
}

void loop() {
    static uint32_t lastHeartbeatMs = 0U;
    const uint32_t nowMs = millis();
    if (!benchmarkComplete ||
        (lastHeartbeatMs != 0U && nowMs - lastHeartbeatMs < 2000U)) {
        delay(25);
        return;
    }
    lastHeartbeatMs = nowMs;
    printResult(lfoResult);
    printResult(recordedShapeResult);
    printResult(adsrResult);
    printResult(dahdsrSharedTrackResult);
    Serial.printf(
        "[modulation-benchmark] heartbeat result=%s\n",
        allCasesWithinBudget() ? "PASS" : "FAIL"
    );
    delay(25);
}
