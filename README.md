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
- sequencer view
- transport and status UI
- persistent macro and sequencer workspaces
- pattern/set library management

It is not a generic plugin SDK, and the repository structure should be read as an embedded application first.

## Build

The main PlatformIO environments are defined in [platformio.ini](platformio.ini):

- `dev`: local symlinked `open-control` dependencies
- `release`: pinned remote dependencies

Typical development build:

```powershell
pio run -e dev
```

Upload to hardware:

```powershell
pio run -e dev -t upload
```

## Repository Layout

The main source tree is:

```text
src/
  config/       platform and timing configuration
  context/      app composition roots and standalone presenters
  handler/      input handling and interaction logic
  persistence/  storage helpers and slot file stores
  sequencer/    runtime playback and clock services
  state/        reactive state and workflows
  ui/           views, components, widgets, top bar, transport bar
```

Development and architecture docs live in [docs/README.md](docs/README.md).

## Architecture

Current architectural direction:

- handlers update state, not LVGL
- views and components render state projections
- context objects assemble modules and scopes
- persistence and workflow logic stay out of widgets
- sequencer runtime is decoupled from heavy UI rendering

For the current architectural audit and cleanup roadmap, start with:

- [docs/ARCHITECTURE_REVIEW.md](docs/ARCHITECTURE_REVIEW.md)
- [docs/INVARIANTS.md](docs/INVARIANTS.md)
- [docs/README.md](docs/README.md)

## Notes

- The codebase is optimized for embedded constraints, so some rendering paths are intentionally imperative.
- Persistence is split between lightweight settings storage and slot-based library storage.
- The authoritative developer index is [docs/README.md](docs/README.md), not historical repository descriptions.
