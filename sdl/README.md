# SDL Desktop And WebAssembly Simulator

Simulator for MIDI Studio Core with LVGL, SDL2, libremidi, and the local
open-control dependencies.

## Quick Start

Run these commands from `midi-studio/core/sdl`.

```bash
# Native desktop build
./build.sh native

# Native build + run
./build.sh run

# WebAssembly build
./build.sh wasm

# WebAssembly build + local server
./build.sh serve
```

`./build.sh serve` serves the generated WASM app on port `8000` and prints the
exact URL. For the core app this is normally:

```text
http://localhost:8000/midi_studio_core.html
```

## Requirements

- Git Bash on Windows, or a Unix-like shell.
- PlatformIO CLI for dependency installation through `pio pkg install`.
- Python 3 for the local HTTP server.
- CMake/Ninja-compatible host tools. On Windows the script downloads Zig,
  Ninja, SDL2, Emscripten, and watchexec under `sdl/tools/` when needed.

## Commands

| Command | Description |
|---|---|
| `./build.sh native` | Build the native desktop simulator. |
| `./build.sh run` | Build and run the native desktop simulator. |
| `./build.sh wasm` | Build the WebAssembly simulator. |
| `./build.sh serve` | Build WASM and serve it on `localhost:8000`. |
| `./build.sh watch native` | Rebuild and restart the native simulator on source changes. |
| `./build.sh watch wasm` | Build WASM and serve it with Emscripten `emrun`. |
| `./build.sh clean` | Remove generated `build/core` and `bin/core` outputs. |

Without arguments, `./build.sh` opens an interactive operation/target menu.

## Native Visual Capture

The workspace `ms` entrypoint is the preferred way to build the native simulator
from any directory:

```powershell
ms build core --target native
```

After that, the native binary can render one frame sequence and save the current
SDL renderer contents to a BMP file:

```powershell
$exe = "C:\Users\simon\Documents\ms-dev-env\bin\core\native\midi_studio_core.exe"
& $exe --capture-bmp .captures\sequencer.bmp --capture-scenario sequencer --capture-scope screen --capture-frames 24
```

`--capture-scope screen` saves only the 320x240 LVGL screen area. Use
`--capture-scope controller` to save the full SDL controller simulator.

Supported capture scenarios:

| Scenario | Surface |
|---|---|
| `macro` | default macro page |
| `macro-edit` | macro edit overlay |
| `macro-page-selector` | macro page selector overlay |
| `sequencer` | default sequencer page |
| `seq-step-edit` | sequencer step edit overlay |
| `seq-property-selector` | sequencer property inline selector |
| `seq-quick-controls` | sequencer quick-control selector |
| `view-selector` | active view selector overlay |
| `settings` | device settings view |
| `data-manager` | Data Manager overlay |
| `data-manager-dialog` | Data Manager command dialog |

## UX Scenario Replay

For integration-style UI checks, replay timestamped logical input events and
capture the screen at chosen moments:

```powershell
$exe = "C:\Users\simon\Documents\ms-dev-env\bin\core\native\midi_studio_core.exe"
& $exe --ux-script sdl\integration\examples\view-selector.ux --ux-output .captures\ux\view-selector
```

The runner writes captures plus `trace.ndjson` in the output directory.
It also writes `binding-trace.ndjson`, an opt-in trace emitted by
`open-control::InputBinding` after scope, authority, predicate, latch, and
gesture logic has evaluated each candidate binding.

UX replay resets the native file-backed storage before each `--ux-script` run
so workflows start from the controller default state. Pass `--ux-keep-storage`
only when a workflow intentionally needs to inspect persisted state from a
previous run.

### UX Workflow Suite

Representative user journeys live under `sdl/integration/workflows/`. They are
plain `.ux` scripts with comments documenting intent, grouped by feature area:

| Workflow group | User path covered |
|---|---|
| `smoke/` | Global view selector and overlay exclusivity smoke checks. |
| `overlays/` | Overlay authority and recovery. |
| `macro/` | Macro performance and edit gestures. |
| `data-manager/` | Data Manager dialogs and command palette flows. |
| `sequencer/editing/` | Step editing, quick controls, and pattern variation editing. |
| `sequencer/runtime/` | Playhead progression and runtime sequencer feedback. |
| `sequencer/settings/` | Project/scale settings workflows. |
| `sequencer/structure/` | Page structure and copy/paste flows. |
| `sequencer/undo-redo/` | Sequencer undo/redo scenarios. |

The canonical runner is the workspace `ms` CLI. List the workflow tree:

```bash
ms ux list core
```

Run the full suite:

```bash
ms ux run core --all
```

Run only a folder. This replays only the workflows inside the selected subtree:

```bash
ms ux run core --select smoke
ms ux run core --select sequencer/undo-redo
```

Run one workflow:

```bash
ms ux run core --select sequencer/undo-redo/step-toggle.ux
```

Regenerate the report from existing workflow outputs without replaying:

```bash
ms ux report core
```

The verifier fails if a workflow does not exit cleanly, misses `trace.ndjson`,
misses `binding-trace.ndjson`, does not write `run_end`, has no dispatched
binding, produces fewer BMP captures than declared in the script, or violates a
declared `# Expect:` semantic assertion.

The report is derived from the `.ux` scripts, `trace.ndjson`,
`binding-trace.ndjson`, and BMP files. It intentionally avoids a separate
manifest so workflow intent, action timing, and capture names stay in one place.

### UX Script Semantics

Script lines use absolute milliseconds from scenario start:

```text
<ms> <command> <arguments>
```

Blank lines are ignored. Full-line and inline comments are supported with `#`
or `//`, so scripts can document user intent beside the replayed action.
Full-line comments can also declare verifier expectations:

```text
# Expect: playhead_progress
```

Supported expectations:

| Expectation | Contract |
|---|---|
| `playhead_progress` | At least two distinct non-negative `playhead_step` values while `playing=true`. |
| `overlay_exclusive` | The standard early/late selector captures remain visually stable within the exclusive-overlay tolerance. |
| `capture_match:<left>=<right>` | The two named BMP captures are byte-identical. |
| `capture_changed:<left>=<right>` | The two named BMP captures differ by at least 16 bytes. |

Multiple expectations can be comma-separated on the same `# Expect:` line.

Supported commands:

| Command | Description |
|---|---|
| `<ms> scenario <name>` | Apply one of the built-in capture scenarios. |
| `<ms> button <ButtonID> down` | Press a logical hardware button. |
| `<ms> button <ButtonID> up` | Release a logical hardware button. |
| `<ms> tap <ButtonID> [duration_ms]` | Expand to button down/up; default duration is 60ms. |
| `<ms> encoder <EncoderID> <delta>` | Turn a logical encoder by a normalized delta. |
| `<ms> capture screen <name>` | Capture only the app screen area. |
| `<ms> capture controller <name>` | Capture the full controller simulator. |
| `<ms> tick` | Pump the app until this time without input. |

Supported button names are `LEFT_TOP`, `LEFT_CENTER`, `LEFT_BOTTOM`,
`BOTTOM_LEFT`, `BOTTOM_CENTER`, `BOTTOM_RIGHT`, `NAV`, and `MACRO_1` through
`MACRO_8`. Supported encoder names are `NAV`, `OPT`, and `MACRO_1` through
`MACRO_8`.

Script timestamps define minimum action intervals. When rendering delays an
action, the runner shifts later actions instead of shortening button holds or
other gestures. `trace.ndjson` records the original `due_ms`, the compensated
`scheduled_ms`, the observed `actual_ms`, capture artifacts, and selected
UI/runtime state snapshots including `playing`, `playhead_step`, and
`sequencer_page`.
`binding-trace.ndjson` records the binding resolution stream:

| Field | Meaning |
|---|---|
| `stage` | `event`, `candidate`, `dispatch`, or `no_dispatch`. |
| `domain` | `button` or `encoder`. |
| `button_id` / `encoder_id` | Logical input id after SDL/hardware mapping. |
| `button_type` / `encoder_type` | Gesture or encoder trigger type. |
| `binding_id` | Runtime binding id assigned by `InputBinding`. |
| `scope_id` | Binding scope; `0` means global. |
| `authority_scope` | Current authority scope used for scoped dispatch. |
| `active` | Result of the binding predicate, including `.when(...)`. |
| `authority` | Whether the binding scope currently owns authority. |
| `required_button` | Encoder required-button/latch condition. |
| `dispatched` | Whether this row is the executed binding. |

Example:

```text
# LEFT_TOP long press opens the View Selector overlay.
100 button LEFT_TOP down
1150 tick
1200 capture screen view_selector_open
1250 button LEFT_TOP up

# Navigate one row and confirm the selected view.
1350 encoder NAV 1
1450 capture screen view_selector_navigated
1500 tap NAV 60
1700 capture screen after_confirm
```

## Structure

```text
sdl/
  app.cmake          app id/name and native/WASM entrypoints
  build.sh           native/WASM build, run, serve, watch, clean script
  CMakeLists.txt     shared native/WASM simulator build
  main-native.cpp    native desktop entrypoint
  main-wasm.cpp      WebAssembly entrypoint
  wasm/shell.html    Emscripten HTML shell
  build/core/        generated CMake/Ninja build trees
  bin/core/          generated native and WASM outputs
  tools/             downloaded local toolchains on Windows
```

## Outputs

Native output:

```text
sdl/bin/core/native/midi_studio_core.exe
```

WASM output:

```text
sdl/bin/core/wasm/midi_studio_core.html
sdl/bin/core/wasm/midi_studio_core.wasm
sdl/bin/core/wasm/midi_studio_core.js
```

## MIDI

Native and WASM entrypoints both use `LibreMidiTransport`. The WASM entrypoint
defaults its bridge URL to `ws://localhost:8100`; native bridge defaults are
defined in the SDL entry helpers and command-line parsing.

## Teensy Parity Notes

The SDL entrypoints share the same application-level feature lane as the Teensy
firmware where the HAL allows it:

| Feature lane | Teensy | SDL native | SDL WASM |
|---|---|---|---|
| Standalone contexts and handlers | `registerContexts(...)` | Same | Same |
| Sequencer runtime/playhead owner | `SequencerRuntimeService` pre-context hook | Same | Same |
| Runtime context gate | `StandaloneSequencerRuntimeHook` | Same | Same |
| MIDI transport | USB MIDI | libremidi native | libremidi WebMIDI |
| Frame/control transport | USB serial frames | UDP frames to bridge | WebSocket frames to bridge |
| Input source | Hardware mux/buttons/encoders | `InputMapper` keyboard/controller simulator | `InputMapper` browser SDL events |
| Persistence storage | SD card backends with hot-swap recovery | File-backed storage | In-memory preview storage |
| UX validation hooks | Hardware/log driven | Capture, replay, binding trace, UX report | Build/runtime parity only |

Known intentional divergences:

- SD-card recovery is Teensy-only because native SDL uses normal files and WASM
  currently uses in-memory preview storage.
- Native SDL has capture/replay/report tooling to validate UX workflows; this is
  simulator instrumentation, not firmware behavior.
- WASM does not currently expose the native UX replay CLI, but it installs the
  same standalone sequencer runtime hook as native SDL and Teensy.

## Troubleshooting

### `emcc` not found

Run a WASM command from Git Bash:

```bash
./build.sh wasm
```

The script installs or activates Emscripten under `sdl/tools/emsdk` if `emcc`
is not already on `PATH`.

### PlatformIO dependencies missing

From `midi-studio/core`:

```bash
pio pkg install
```

The SDL build also attempts this automatically when `.pio/libdeps` is missing.

### Clean generated simulator outputs

```bash
./build.sh clean
```

For a complete local-tool cleanup, remove `sdl/tools/` manually as well.

## Technical Notes

- SDL2 is provided by Emscripten for WASM via `-s USE_SDL=2`.
- WASM uses `sdl/wasm/shell.html` as the Emscripten shell file.
- Native and WASM builds share `sdl/CMakeLists.txt`.
- The simulator uses the same 480x320 LVGL display shape as the standalone UI.
