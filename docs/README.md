# MIDI Studio Core Docs

Developer documentation for the standalone firmware in this repository.

## Start Here

Read these first:

1. [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)
2. [INVARIANTS.md](INVARIANTS.md)
3. [CODE_STYLE.md](CODE_STYLE.md)
4. [EXTENSION_CHECKLIST.md](EXTENSION_CHECKLIST.md)
5. [ARCHITECTURE_REVIEW_RULES.md](ARCHITECTURE_REVIEW_RULES.md)

## Current Architecture References

- [CODEBASE_CLEANUP_AUDIT.md](CODEBASE_CLEANUP_AUDIT.md)
- [CORE_ALIGNMENT_ROADMAP.md](CORE_ALIGNMENT_ROADMAP.md)
- [SEQUENCER_CODEBASE_CLEANUP_AUDIT.md](SEQUENCER_CODEBASE_CLEANUP_AUDIT.md)
- [SEQUENCER_INLINE_WORKFLOW_ROADMAP.md](SEQUENCER_INLINE_WORKFLOW_ROADMAP.md)

These documents reflect the current cleanup and refactor direction more accurately than older high-level descriptions.

## Practical Guides

- [STATE_MANAGEMENT.md](STATE_MANAGEMENT.md)
- [HOW_TO_ADD_WIDGET.md](HOW_TO_ADD_WIDGET.md)
- [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md)
- [HOW_TO_ADD_VIEW.md](HOW_TO_ADD_VIEW.md)
- [HOW_TO_ADD_OVERLAY.md](HOW_TO_ADD_OVERLAY.md)

## Persistence Notes

- [SD_PERSISTENCE_TRACKER.md](SD_PERSISTENCE_TRACKER.md)
- [SD_PERSISTENCE_HARDWARE_VALIDATION.md](SD_PERSISTENCE_HARDWARE_VALIDATION.md)

## Source Map

Main code areas in this repo:

```text
src/
  config/       configuration and platform constants
  context/      composition roots, overlay presenters, runtime wiring
  handler/      input logic and interaction workflows
  persistence/  slot-based persistence helpers
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

## Downstream Check

Before changing exported headers or moving files consumed by other repos, run:

```powershell
pwsh ./script/dev/check-downstream-compat.ps1
```

This rebuilds `plugin-bitwig` against the current `ms-core` checkout and catches public include regressions earlier.
