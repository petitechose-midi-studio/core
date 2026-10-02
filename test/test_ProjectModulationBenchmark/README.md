# Project modulation contract and host baseline

The default executable validates the exact maximum workload and budget contract.
`--benchmark` runs the same four workloads with 64 warmup frames and 2048 measured
frames, five independent trials each. Each trial starts with a freshly prepared
workspace. Output is CSV: case, trial, frames, integer average microseconds,
maximum microseconds, checksum and trigger tests per frame. Preparation and
printing are outside the measured interval. Invalid arguments return exit 2.

Build `test_ProjectModulationBenchmark` in a separate **Release** CMake build tree
using the same toolchain and dependency roots as the native tests, then run:

```text
test_ProjectModulationBenchmark
test_ProjectModulationBenchmark --benchmark
```

The normal `ms test core` runner uses Debug; do not compare those timings to
Release. Assertions remain enabled in this test, including the opt-in mode.
Record the revision, compiler, flags, CPU, dependency roots and binary hash with
the raw CSV. Compare equivalent builds on the same host, without concurrent
builds. Keep all trials, including maximum-time outliers; integer averages have
microsecond quantization. Stable checksums verify repeatability of the workload,
not equivalence of every intermediate value.

These are host wall-clock measurements of modulation evaluation, not Teensy
latency, end-to-end MIDI timing, rendering, storage or user-input response times.
The MCU budget is deliberately not used as a host pass/fail threshold. For real
device measurements, follow `script/bench/README.md` and retain the firmware and
fixture identity; the RAM-only hardware fixture does not measure SD operations.
