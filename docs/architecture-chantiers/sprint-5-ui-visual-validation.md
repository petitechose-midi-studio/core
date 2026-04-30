# Sprint 5: UI Visual Validation And Render Maintainability

## Goal

Make the main LVGL surfaces reviewable with repeatable native captures before
changing UI rendering code further.

## Current Status

Status as of 2026-04-30: capture, UX replay, binding trace, and workflow smoke
suite are implemented. Visual baseline reporting and richer semantic assertions
remain future work.

The native SDL app now supports two validation paths:

- a direct capture mode that renders a named scenario for a fixed number of
  frames and writes a BMP artifact;
- a timestamped UX scenario runner that injects logical `ButtonID`/`EncoderID`
  events, captures selected moments, and writes a replay trace.

## Entry Points

Build from any workspace location through `ms`:

```powershell
ms build core --target native
```

Capture a scenario:

```powershell
$exe = "C:\Users\simon\Documents\ms-dev-env\bin\core\native\midi_studio_core.exe"
& $exe --capture-bmp .captures\sequencer.bmp --capture-scenario sequencer --capture-scope screen --capture-frames 24
```

Replay a timed UX path:

```powershell
$exe = "C:\Users\simon\Documents\ms-dev-env\bin\core\native\midi_studio_core.exe"
& $exe --ux-script sdl\integration\examples\view-selector.ux --ux-output .captures\ux\view-selector
```

Run the curated workflow suite:

```powershell
sdl\integration\run-ux-workflows.ps1
```

Run the suite and generate the derived UX report:

```powershell
sdl\integration\run-ux-workflows.ps1 -Report
```

Regenerate only the report from existing artifacts:

```powershell
sdl\integration\generate-ux-report.ps1
```

The local `.captures/` directory is ignored by Git and is intended for review
artifacts only.

## Covered Scenarios

Current named scenarios:

- `macro`
- `macro-edit`
- `macro-page-selector`
- `sequencer`
- `seq-step-edit`
- `seq-property-selector`
- `seq-quick-controls`
- `view-selector`
- `settings`
- `data-manager`
- `data-manager-dialog`

## First Validation Result

Native capture smoke result on 2026-04-29:

- `ms build core --target native` passed.
- All listed scenarios produced BMP files.
- Captures were 1053x1053 and non-empty by sampled pixel diversity.
- Manual inspection confirmed that representative macro, sequencer step edit,
  and Data Manager dialog surfaces render visible UI.
- The UX runner replayed `sdl/integration/examples/view-selector.ux`, produced
  320x240 screen-only captures, and wrote `trace.ndjson` with due time, actual
  time, drift, action, logical input id, and capture path.
- `open-control::InputBinding` now supports an opt-in trace callback. Native UX
  runs write `binding-trace.ndjson`, including logical event, candidate binding,
  predicate result, authority result, required-button result, and dispatched
  binding rows.
- `sdl/integration/workflows/` now covers view switching, macro edit, sequencer
  step edit, sequencer quick controls, Data Manager dialog flow, and overlay
  priority/recovery.
- `sdl/integration/run-ux-workflows.ps1` verifies each workflow exits cleanly,
  writes `trace.ndjson` and `binding-trace.ndjson`, reaches `run_end`, dispatches
  at least one binding, and produces the declared capture artifacts.
- `sdl/integration/generate-ux-report.ps1` derives a Markdown report from the
  workflow scripts, replay traces, binding traces, and BMP files. The `.ux`
  script remains the source of truth for workflow intent, timing, and capture
  names.

## Fragility Points To Watch

- Most LVGL widget code is built by the native target but not exercised by
  `ms test core`; the test suite mainly covers model/projection logic.
- Binding-level tracing lives in `open-control::InputBinding`; SDL only writes
  the callback stream to disk. This keeps future hardware replay aligned with
  native replay instead of duplicating dispatch knowledge in the simulator.
- Overlay surfaces are semi-transparent over live views, so visual regressions
  can appear as overlap, stale background state, or poor contrast rather than
  direct logic failures.
- `MacroView` and `SequencerView` both rely on dirty flags, timers, and
  subscribed state changes; missed invalidation can create stale visual state.
- Step-grid drawing remains the highest-risk custom rendering branch because it
  combines geometry, labels, and manual draw descriptors.
- Data Manager and settings overlays swap softkey/transport affordances, making
  them high-value capture targets.

## Next Actions

- Review and curate the derived UX report as the first visual reference report.
- Decide which visual contracts are stable enough for automated comparison.
- Add only focused assertions after manual baselines exist, for example image
  dimensions, non-empty frame, and selected scenario visibility.
- Add symbolic names for scope ids / handlers if trace review shows raw runtime
  ids are too hard to read.
- Keep render refactors scoped to places where capture evidence shows real
  fragility or where existing projection code is already duplicated.

## Exit Signal

Sprint 5 is complete when main screens and overlays have a repeatable capture
path, a checked visual reference report, and at least one automated smoke guard
that fails on blank or missing UI output.
