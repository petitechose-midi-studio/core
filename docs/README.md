# MIDI Studio Core Docs

Developer documentation for the standalone firmware in this repository.

## Start Here

Read these first, in order:

1. [ARCHITECTURE_REVIEW_RULES.md](ARCHITECTURE_REVIEW_RULES.md)
2. [INPUT_BINDINGS.md](INPUT_BINDINGS.md)
3. [CODE_STYLE.md](CODE_STYLE.md)

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
  config/       configuration and platform constants
  context/      composition roots, overlay presenters, runtime wiring
  handler/      input logic and interaction workflows
  persistence/  product filesystem, project/session transactions, and codecs
  sequencer/    playback and clock services
  state/        reactive state, workflows, bootstrap, lifecycle
  ui/           views, components, widgets, top bar, transport bar
```

## Ground Rules

- handlers update state, not LVGL
- views render projections of state
- transient UI lifecycle belongs in state/runtime, not in widgets when avoidable
- workflows and persistence logic should stay out of components
- avoid generic abstractions unless duplication is both real and stable

## Unit Tests

Use the workspace `ms` command as the test entry point:

```powershell
ms test core
```

`ms` resolves the correct workspace, so it can be launched from this repository
or elsewhere in the `ms-dev-env` checkout. This runs the core unit tests through
the CMake/CTest backend. PlatformIO remains the firmware build/upload path, not
the primary local unit-test runner.

## Downstream Check

Before changing exported headers or moving files consumed by other repos, run:

```powershell
pwsh ./script/dev/check-downstream-compat.ps1
```

This rebuilds `plugin-bitwig` against the current `ms-core` checkout and catches public include regressions earlier.
