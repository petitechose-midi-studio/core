# Hardware benchmark endpoint contract

This isolated native executable uses the production endpoint, run engine, cold
fixture, CoreState, EventBus, metrics sink, semantic snapshot/provider machinery,
and LVGL frame collector. Only transport, physical buttons, time, and allocator
monitor values are simulated. No socket, serial port, MIDI device, SD or file
storage is opened. Tests invoke reset's native branch; they cannot reboot a host.

Build the entire native graph with stats enabled: NotificationQueue's ABI changes
with that flag. The file is deliberately named `endpoint_test.cpp`, outside the
normal Core `test_main.cpp` glob, to avoid linking it against a stats-off library.

```sh
cmake -S test/test_HardwareBenchmarkEndpoint -B ../../.build/core-benchmark-endpoint -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../../.build/core-benchmark-endpoint --target test_HardwareBenchmarkEndpoint
ctest --test-dir ../../.build/core-benchmark-endpoint --output-on-failure
```

Run from the Core checkout; choose any separate build directory. CMake 3.29+,
C++17 and Python 3.9+ are required. If necessary specify the installed compiler
and Ninja with `CMAKE_CXX_COMPILER` and `CMAKE_MAKE_PROGRAM`.

Checks cover framing/request IDs, malformed requests, exact upload retries,
one program per boot, start retries and physical-held refusal, one action per
foreground turn, held-state visibility to observers, real bus/state changes,
passive semantic modes, assertions/deadlines, cancellation/reset release, early
result rejection, delayed allocation inspection, foreign/physical contamination,
all result pages and bounds, stable snapshots, and JSON-safe mode identifiers.
Busy-profiler and notification-overflow failures and microsecond wrap are also
checked; cancellation before the first capture serializes no uninitialized metric.
Physical events before upload/start and incoming MIDI before start, while armed,
while running, and during post-run cleanup invalidate the run. Synthetic events
and their cleanup releases remain uncontaminated. Physical/MIDI and notification
overflow counters are frozen when results become ready, including overflows that
first happen during cleanup. The HAL MIDI counter getter is simulated here;
USB input polling and raw GPIO activity are not emulated.
The Python runner parses every emitted response payload with `json.loads` after
the C++ assertions. It does not replace the packet/header assertions.
The 64-bit serialization regression accumulates 4,500,000,000 us through the real
metrics sink, then seeds a stopped LVGL snapshot with UINT64_MAX to exercise all
20 digits in each result page. JSON numeric types, exact totals, histogram bins,
and subsequent format arguments are checked. These seeded LVGL values are only
a serializer boundary fixture, not a possible run. Native libc does not emulate
Teensy's nano printf limitations; the firmware needs its own wire smoke test.

This is a correctness test, not LVGL rendering, USB framing, ISR latency, hardware
reset, or a measurement of Teensy performance. No durations from the fake clock
are performance evidence.
