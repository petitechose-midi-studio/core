# Hardware benchmark metric collector checks

This test links only `HardwareBenchmarkMetrics.cpp`, framework
`diagnostics/Performance.cpp`, and `time/Time.cpp`, with C++17 and
`MS_HARDWARE_BENCHMARK=1`, `OC_ENABLE_STATS=1`, assertions enabled. Include
Core `src/` and framework `src/`. No LVGL, hardware, storage, logger or full
Core aggregate is needed.

Checks cover every whitelist entry, equal labels at different addresses,
unknown/null names, count/sum/max/unit maxima, logarithmic bucket boundaries,
first-maximum timestamp (including zero duration and ties),
32-bit clock wrap, inferred pre-start rejection, real PerformanceScope
lifetimes across start/stop, reset, and a callback already dispatched before
`endMetrics()` freezes capture. The latter uses the fake clock to stop
collection before the callback's guarded mutation, then verifies no writes.

The collector uses a static `DMAMEM` metric array sized to the whitelist, no
queue, no histograms in PSRAM, and no reporting from the sink. Label lookup and
clock reads occur before the IRQ-masked mutation section. Only bounded scalar
updates occur under `InterruptGuard`; 64-bit totals cannot tear between
foreground and IRQ writers. Lifecycle calls are foreground-only; read metrics
and `boundarySkipped()` only after `endMetrics`. This is a single-core IRQ
contract, not a native multi-thread lock.

The immutable whitelist is sorted and unique, checked at compile time. Lookup
uses `std::lower_bound`, including for rejected labels, rather than scanning
every name for every sample. No dynamic cache, allocation, or additional
critical section is needed. Results identify metrics by name; enumeration
indices are not stable across firmware versions.

From the development environment root, the standalone checks can be built
without the rest of Core (PowerShell):

```powershell
tools/zig/zig.exe c++ -std=c++17 -O2 -DOC_ENABLE_STATS=1 -DMS_HARDWARE_BENCHMARK=1 -I midi-studio/core/src -I open-control/framework/src midi-studio/core/test/test_HardwareBenchmarkMetrics/test_main.cpp midi-studio/core/src/validation/benchmark/HardwareBenchmarkMetrics.cpp open-control/framework/src/oc/diagnostics/Performance.cpp open-control/framework/src/oc/time/Time.cpp -o .build/test_HardwareBenchmarkMetrics.exe
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
.build/test_HardwareBenchmarkMetrics.exe
```

The existing PerformanceSample ABI contains duration but no original start
timestamp. The collector therefore excludes samples where
`receivedAtUs - elapsedUs` appears earlier than the run start and reports their
count through `boundarySkipped()`. This is **approximate**: dispatch latency
and IRQ preemption before callback receipt can move the inferred origin.
It does not guarantee exact exclusion of every pre-start scope. Clock/run
comparisons require runs shorter than 2^31 microseconds (about 35 minutes).
The intended bench runs are much shorter.

`bins[0]` counts zero durations; `bins[n]`, n=1..32, represents
`[2^(n-1), 2^n-1]` µs. Returned values are the selected producer's reported
durations/units: interval and queue-age metrics are not execution spans.
`maxAtUs` is the first maximum's callback-receipt time relative to run start;
it can be correlated with event/LVGL timestamps but is not the sample's start.
Counters saturate rather than wrap. `metricCount()` is zero before the first
capture so a canceled-before-start endpoint cannot serialize null names.
Starting another capture resets everything while the performance sink is
detached, then installs the benchmark sink under a short guard. The ordinary
PerformanceReporter must remain disabled in this benchmark image.
