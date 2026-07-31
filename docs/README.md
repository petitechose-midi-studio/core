# MIDI Studio Core Docs

Developer documentation for the standalone firmware in this repository.

## Start Here

Read these first, in order:

1. [DEVELOPER_ONBOARDING.md](DEVELOPER_ONBOARDING.md)
2. [CORE_ARCHITECTURE.md](CORE_ARCHITECTURE.md)
3. [INPUT_BINDINGS.md](INPUT_BINDINGS.md)
4. [ARCHITECTURE_REVIEW_RULES.md](ARCHITECTURE_REVIEW_RULES.md)
5. [CODE_STYLE.md](CODE_STYLE.md)

Architecture contracts should live as close as possible to the code they
constrain. Put durable "why" comments in `.hpp` files when a reader needs the
contract before editing an API. Keep `.cpp` comments short and local to
non-obvious implementation details.

Codebase-scale plans, ADRs, and audit evidence belong in the canonical
[petitechose-audio-docs](https://github.com/petitechose-audio/petitechose-audio-docs)
repository. This directory contains only code-local developer documentation.

## Source Map

Main code areas in this repo:

```text
src/
  api/          small product facades over OpenControl input APIs
  app/          application allocation and shared app-level types
  config/       configuration, timing and physical input IDs
  context/      composition roots, scopes, overlays, presenters and wiring
  diagnostics/  removable performance and memory instrumentation
  handler/      input interpretation and domain workflows
  midi/         MIDI transport/runtime helpers
  persistence/  product files, stores, codecs and atomic transactions
  protocol/     controller/host protocol
  sequencer/    realtime playback, clocks and MIDI frame coordination
  state/        canonical state, invariants, snapshots and domain policies
  ui/           view models, retained views, widgets and render projections
  validation/   semantic validation and smoke surfaces
```

## Ground Rules

- handlers update state and domain services, not LVGL
- views render read-only projections
- context owns composition, scope and overlay presentation
- persistence depends on domain state, not handlers or UI
- realtime services remain independent of menus, files and LVGL
- factorize a mechanical policy only when its behavior and invariant are
  genuinely shared

## Unit Tests

Use the workspace `ms` command as the test entry point:

```powershell
ms test core
```

`ms` resolves the correct workspace, so it can be launched from this repository
or elsewhere in the `ms-dev-env` checkout. This runs the core unit tests through
the CMake/CTest backend. PlatformIO remains the firmware build/upload path, not
the primary local unit-test runner.

## Architecture Gate

Run the fast repository contract before the native suite:

```powershell
python script/dev/check-architecture-contracts.py
```

Use `--inventory` for the complete advisory list of files over 800 physical
lines and `--self-test` to run the gate's deterministic fixtures.

## Downstream Check

Before changing exported headers or moving files consumed by other repos, run:

```powershell
pwsh ./script/dev/check-downstream-compat.ps1
```

This rebuilds `plugin-bitwig` against the current `ms-core` checkout and catches public include regressions earlier.
