# Autonomous hardware baseline

These `.ux` files target **only** `dev_hardware_benchmark`, with the
`bench-macro-v1` fixture. They are outside the SDL workflow catalog deliberately:
`assert_view`, `assert_mode`, `marker`, and `transport` are hardware-bench syntax.
The wire contract and parser details are in the workspace's
[hardware-ux-protocol-v1.md](../../../../docs/hardware-ux-protocol-v1.md).

The fixture reuses the existing `macro-multi-modulation` setup: Macro 1 has three
shared LFO assignments, with Pulse Lift focused. It adds a 16-step note pattern
and a non-conflicting CC1 lane on Track 1; Track 2 also receives the shared Slow
Tide assignment. The timer, musical computation, UI and foreground MIDI output
remain active. Settings are RAM-only and product storage is unavailable from
boot. There is **no SD load/save/autosave workload** in these trials; this is not
a reproduction of storage-related timing or an arbitrary user's project.

Unlike `dev_ux_diagnostics`, this is a dedicated measurement image: no normal
`PerformanceReporter`, serial log formatting, or UX recorder flush runs during
capture. Fixed aggregate counters retain the worst LVGL frame and bounded
timing histograms; memory tracking stays enabled. Physical driver polling,
450 MHz CPU configuration, display cadence and musical timer configuration
remain those of the development profile. Scripted held buttons are visible to
the normal ButtonAPI; physical input contamination invalidates a trial.

The benchmark-specific ELF gate requires the endpoint and profilers, checks
histogram storage in RAM2 and the authoritative span table in EXTRAM, and
rejects linked SD/recovery/autosave/normal reporter/log-formatting code. All
remaining product placement and physical ITCM/RAM1/RAM2 capacity gates still
apply. Passing these software/build checks does not establish physical USB
MIDI timing; external capture remains a separate qualification.

## Build and flash separately

From the `ms-dev-env` workspace root, after local prerequisites are installed:

```powershell
ms build core --target teensy --env dev_hardware_benchmark --stream
```

The build keeps the normal memory gates and exports
`bin/core/teensy/dev_hardware_benchmark/firmware.hex`. The CLI does not flash as
part of `ms ux hardware`. Before flashing, preserve any live unsaved user work
and the previously used firmware artifact/profile. An MCU reboot destroys RAM.

Use MS Manager's **selected controller** workflow: Workspace artifacts,
Standalone target, profile `dev_hardware_benchmark`, **Build Firmware**, then
**Flash**. The current Elegoo binding is `bitwig-hardware-18040250`, serial
`18040250`, control port `8001`; verify the live identity instead of relying on
the display name. Manager's `flash_bridge_instance(instance_id, build_profile)`
resolves that binding's exact serial to a loader device ID, pauses/resumes its
bridge, and checks reconnect. A stale artifact is possible after source edits,
so build explicitly before flashing.

Do not substitute generic `ms upload core` in a multi-controller workspace:
that command currently has no serial/instance selector. A missing or mismatched
benchmark HELLO is a stop condition, not permission to flash another device.
After testing, restoring normal firmware is a separate, explicitly selected
flash operation; benchmark reboot does not restore another firmware image.

## Identify, run, retrieve

These examples run from the workspace root. The first command reads bridge
state only; the second also queries the compatible benchmark endpoint. Neither
starts a trial. Do not poll during a running measurement.

```powershell
.\open-control\bridge\target\release\oc-bridge.exe ctl --control-port 8001 info
ms ux hardware status --serial 18040250 --control-port 8001
```

Require `serial_open:true`, the intended serial, and benchmark
`ram_only:true`, `requires_reboot:true`, fixture `bench-macro-v1`. Run with no DAW
traffic and no physical input. Choose fresh output paths every time:

```powershell
ms ux hardware run midi-studio/core/script/bench/idle-playback.ux --serial 18040250 --control-port 8001 --output-root .bench/idle-baseline --fresh --repeat 3 --max-lateness-us 1000000
ms ux hardware run midi-studio/core/script/bench/modulator-transitions.ux --serial 18040250 --control-port 8001 --output-root .bench/transitions-baseline --fresh --repeat 3 --max-lateness-us 1000000
ms ux hardware run midi-studio/core/script/bench/macro-edit.ux --serial 18040250 --control-port 8001 --output-root .bench/edit-baseline --fresh --repeat 3 --max-lateness-us 1000000
```

`--fresh` explicitly reboots verified benchmark firmware before trial 1;
`--repeat` reboots between trials and refuses a changed build or fixture.
The host uploads all inputs, then stays silent through duration + allowed
lateness + arm/cleanup time. Inputs execute on the MCU with fixed absolute due
times; a late render never stretches the rest of the gesture schedule.

| Workload | Measured duration | Events | Purpose |
|---|---:|---:|---|
| idle-playback | 10 s | 11 | Macro root with running fixture music; no edits |
| modulator-transitions | 20 s | 108 | Eight assignment/source round trips, plus parent reconstruction |
| macro-edit | 20 s | 67 | Repeated signed Depth edits on the existing Pulse Lift assignment |

The transition and edit gestures come from the existing
`modulator-source-deep-link.ux` and `modulator-multi-assignment.ux` workflows.
NAV `encoder_value 0.010` is intentional: the real navigation handler uses the
sign of a nonzero event to move one row. OPT inputs are absolute normalized
values, not approximated relative deltas. Assertions verify both view and mode
at each target. Markers label the following transition/work interval; they are
not screenshots or physical GPIO triggers.

Each `run-NNN` directory retains `manifest.json`, `summary.json`, and
`results.ndjson`. The manifest records original script SHA-256, wire-record
FNV-1a, duration/lateness budget, bridge serial/instance, firmware source build
ID and fixture version. A successful summary is required for an accepted trial;
inspect failure, lateness, foreign requests, notification/LVGL errors, memory,
timer/CC metrics and worst LVGL frame instead of comparing one maximum alone.
The build ID is not a HEX-file hash. Keep the built artifact separately.

Ctrl+C requests cancellation and retains partial host evidence. If that request
times out, cancellation is **not confirmed**; inspect status before proceeding.
Rebooting loses MCU results that have not been retrieved. Normal cancellation
releases injected buttons and stops the benchmark transport.

These are internal software timings, **not** measurements of physical USB MIDI
edge timing, end-to-end DAW jitter, audio timing, display scanout, or SD latency.
Those require separate external capture and a stated physical acceptance limit.
