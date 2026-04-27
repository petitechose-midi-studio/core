# MIDI Studio Core Docs

Developer documentation for the standalone firmware in this repository.

## Start Here

Read these first:

1. [CORE_ARCHITECTURE_AUDIT_2026_04.md](CORE_ARCHITECTURE_AUDIT_2026_04.md)
2. [CORE_REFACTOR_BACKLOG_2026_04_NEXT.md](CORE_REFACTOR_BACKLOG_2026_04_NEXT.md)
3. [STANDALONE_LIFECYCLE_CONTRACT.md](STANDALONE_LIFECYCLE_CONTRACT.md)
4. [INVARIANTS.md](INVARIANTS.md)
5. [ARCHITECTURE_REVIEW_RULES.md](ARCHITECTURE_REVIEW_RULES.md)
6. [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)
7. [CODE_STYLE.md](CODE_STYLE.md)
8. [EXTENSION_CHECKLIST.md](EXTENSION_CHECKLIST.md)

## Current Architecture References

- [CORE_ARCHITECTURE_AUDIT_2026_04.md](CORE_ARCHITECTURE_AUDIT_2026_04.md)
- [CORE_REFACTOR_BACKLOG_2026_04_NEXT.md](CORE_REFACTOR_BACKLOG_2026_04_NEXT.md)
- [INVARIANTS.md](INVARIANTS.md)
- [ARCHITECTURE_REVIEW_RULES.md](ARCHITECTURE_REVIEW_RULES.md)
- [STANDALONE_LIFECYCLE_CONTRACT.md](STANDALONE_LIFECYCLE_CONTRACT.md)
- [STANDALONE_INTERACTION_GRAMMAR_SPEC.md](STANDALONE_INTERACTION_GRAMMAR_SPEC.md)
- [SEQUENCER_ACTION_STRIP_SPEC.md](SEQUENCER_ACTION_STRIP_SPEC.md)
- [REALTIME_MIDI_ISOLATION_PLAN.md](REALTIME_MIDI_ISOLATION_PLAN.md)
- [REALTIME_MIDI_REFACTOR_EXECUTION_PLAN.md](REALTIME_MIDI_REFACTOR_EXECUTION_PLAN.md)
- [REALTIME_MIDI_FEASIBILITY_REPORT.md](REALTIME_MIDI_FEASIBILITY_REPORT.md)
- [EXTERNAL_CLOCK_INGRESS_STRATEGY.md](EXTERNAL_CLOCK_INGRESS_STRATEGY.md)

These files are the normative entry points for current naming, placement, and responsibility boundaries.

## Practical Guides

These guides are intentionally illustrative. They explain patterns, but they are not the source of truth for current naming, constructor signatures, or composition wiring.

- [STATE_MANAGEMENT.md](STATE_MANAGEMENT.md)
- [HOW_TO_ADD_WIDGET.md](HOW_TO_ADD_WIDGET.md)
- [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md)
- [HOW_TO_ADD_VIEW.md](HOW_TO_ADD_VIEW.md)
- [HOW_TO_ADD_OVERLAY.md](HOW_TO_ADD_OVERLAY.md)

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
