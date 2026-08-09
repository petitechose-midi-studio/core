# MIDI Studio Core

Firmware standalone for the MIDI Studio hardware on Teensy 4.1.

This repository contains the embedded app, UI, state, handlers, persistence, and sequencer runtime used by the device. It is built with PlatformIO and LVGL, and integrates local `open-control` libraries in development.

## Release Policy

- `core` is a producer repo for signed firmware candidates.
- Its canonical release-grade output is the exact signed candidate artifact for the firmware it
  owns.
- End-user system publication remains centered on `petitechose-midi-studio/distribution`, which
  consumes the exact signed firmware candidate.
- This repo does not require an independent end-user final release surface.

## Scope

This repo currently targets the standalone device workflow:

- macro view
- step-sequencer and project/modulator views
- invariant transport and context-scoped controller navigation
- project/session persistence and file-backed Step/Chord preset libraries
- Macro Automation, Modulation, CC Lanes and bounded graph editing
- native/WASM simulation and semantic UX validation

It is not a generic plugin SDK, and the repository structure should be read as an embedded application first.

## Build

The main PlatformIO environments are defined in [platformio.ini](platformio.ini):

- `dev`: local symlinked `open-control` dependencies
- `dev_diagnostics`: product behavior with removable performance/memory
  instrumentation
- `dev_drum_track_ux`: opt-in Teensy build for the temporary Drum Track UX
  prototype
- `dev_ux_recorder`: explicit validation build with Teensy semantic UX logs
- `release`: pinned remote dependencies

Typical development build:

```powershell
ms build core --target teensy --env dev
```

Validation build with semantic UX recorder:

```powershell
ms build core --target teensy --env dev_ux_recorder
```

Temporary Drum Track UX firmware:

```powershell
pio run -e dev_drum_track_ux
pio run -e dev_drum_track_ux -t upload
```

Upload to hardware:

```powershell
ms upload core --env dev
```

## Unit Tests

The supported local unit-test entry point is the workspace `ms` command:

```powershell
ms test core
```

`ms` resolves the correct workspace, so the command is valid from this
`midi-studio/core` directory or from elsewhere in the `ms-dev-env` checkout.
PlatformIO remains the firmware build/upload path; unit tests are run through CMake/CTest so
local and CI execution use the same native test backend, workspace-pinned tools, and pinned
test dependencies.

## Validation Checklist

For sequencer, state, UI, persistence, or `open-control` integration changes,
validate the touched dependency repos before the Core firmware build:

```powershell
ms test open-control-framework
ms test open-control-note
ms test core
ms build core --target teensy --env dev
```

For semantic UX coverage, run focused workflows through the workspace UX runner,
for example:

```powershell
ms ux run core --select sequencer/editing/step-edit-chord.ux
ms ux run core --select sequencer/editing/step-edit-chord-strum.ux
```

### Semantic UX ownership

`src/validation/ux` owns the product-agnostic semantic validation port, data
types, registry and recorder. `src/context/standalone/ux` owns the
product-specific adapters that read Standalone state and project it onto those
generic semantic surfaces. The dependency therefore flows from Standalone
adapters to the validation port, never in the reverse direction. Recorder
instrumentation remains behind `MS_UX_RECORDER` and is absent from normal
firmware builds; firmware enables it only in the dedicated `dev_ux_recorder`
environment.

The `ms release dependencies --dry-run` command is an alignment/readiness check:
it reports dirty repos and the dependency promotion plan, but does not run tests
or mutate dependency pins. Once dependency repos are clean and merged, use the
release helper to promote pins; do not edit dependency SHAs by hand.

## Repository Layout

The main source tree is:

```text
src/
  api/          product facades over OpenControl input APIs
  app/          application allocation and shared app-level types
  config/       constants, timing and physical input IDs
  context/      composition, scopes, overlays, presenters and wiring
  diagnostics/  removable performance and memory reporting
  handler/      input interpretation and domain workflows
  midi/         MIDI transport/runtime helpers
  persistence/  files, stores, codecs and atomic transactions
  protocol/     controller/host transport protocol
  sequencer/    realtime playback, clocks and MIDI frame coordination
  state/        canonical state, invariants, snapshots and domain policies
  ui/           view models, retained views and render projections
  validation/   semantic validation and smoke surfaces
```

Development and architecture docs live in [docs/README.md](docs/README.md).

## Generated Assets

Icon sources live in [asset/icon](asset/icon). The standalone icon font and C++
font data are generated artifacts that must be committed together with their
source SVG changes:

- [asset/font/standalone_icons.ttf](asset/font/standalone_icons.ttf)
- `src/ui/font/data/standalone_icons_*.{hpp,c.inc}`
- [src/ui/font/StandaloneIcons.hpp](src/ui/font/StandaloneIcons.hpp)

Workspace-local build outputs, scratch directories, and generated binaries
belong outside Core commits. In the shared `ms-dev-env` checkout, `.tmp/` and
`bin-*/` are ignored at the workspace root.

## Architecture

Current architectural direction:

- handlers update state, not LVGL
- views and components render state projections
- context objects assemble modules and scopes
- persistence and workflow logic stay out of widgets
- sequencer runtime is decoupled from heavy UI rendering

For code-local contracts and review rules, start with:

- [docs/README.md](docs/README.md)
- [docs/DEVELOPER_ONBOARDING.md](docs/DEVELOPER_ONBOARDING.md)
- [docs/CORE_ARCHITECTURE.md](docs/CORE_ARCHITECTURE.md)
- [docs/ARCHITECTURE_REVIEW_RULES.md](docs/ARCHITECTURE_REVIEW_RULES.md)

Cross-repository roadmaps, ADRs, and audit evidence live in the canonical
[petitechose-audio-docs](https://github.com/petitechose-audio/petitechose-audio-docs)
repository. They are intentionally not duplicated in Core.

## Notes

- The codebase is optimized for embedded constraints, so some rendering paths are intentionally imperative.
- Settings use lightweight storage; projects, sessions, and reusable assets use
  versioned product files under the shared MIDI Studio filesystem.
- The authoritative developer index is [docs/README.md](docs/README.md), not historical repository descriptions.
