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
| `settings` | global settings overlay |
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

### UX Workflow Suite

Representative user journeys live under `sdl/integration/workflows/`. They are
plain `.ux` scripts with comments documenting intent:

| Workflow | User path covered |
|---|---|
| `view-selector-global.ux` | Global view selector open, navigation, confirm. |
| `macro-edit-adjust.ux` | Macro performance value, long-press edit, field adjust, close. |
| `sequencer-step-edit.ux` | Step toggle, long-press step edit, value adjust, close. |
| `sequencer-quick-controls.ux` | Held quick controls, NAV selection, OPT edit, release apply. |
| `data-manager-dialog.ux` | Data Manager navigation, command palette dialog, clean close. |
| `overlay-priority-recovery.ux` | Overlay authority, unrelated input isolation, recovery after close. |

Run the full suite from anywhere in the repo after a native build:

```powershell
sdl\integration\run-ux-workflows.ps1 -SkipBuild
```

Or let the script build through the workspace `ms` entrypoint first:

```powershell
sdl\integration\run-ux-workflows.ps1
```

Add `-Report` to write a Markdown UX report next to the workflow artifacts:

```powershell
sdl\integration\run-ux-workflows.ps1 -Report
```

To regenerate the report from existing workflow outputs without replaying:

```powershell
sdl\integration\generate-ux-report.ps1
```

The verifier fails if a workflow does not exit cleanly, misses `trace.ndjson`,
misses `binding-trace.ndjson`, does not write `run_end`, has no dispatched
binding, or produces fewer BMP captures than declared in the script.

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

`trace.ndjson` records replay timing and capture artifacts. `binding-trace.ndjson`
records the binding resolution stream:

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
