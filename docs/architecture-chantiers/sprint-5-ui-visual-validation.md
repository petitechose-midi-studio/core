# Sprint 5: UI Visual Validation And Render Maintainability

## Goal

Make the main LVGL surfaces reviewable with repeatable native captures before
changing UI rendering code further.

## Current Status

Status as of 2026-04-30: capture, UX replay, binding trace, derived UX report,
workflow smoke suite, and the SDL playhead-progression guard are implemented.
Future work should focus on stable visual comparison contracts rather than more
capture plumbing.

The native SDL app now supports two validation paths:

- a direct capture mode that renders a named scenario for a fixed number of
  frames and writes a BMP artifact;
- a timestamped UX scenario runner that injects logical `ButtonID`/`EncoderID`
  events, captures selected moments, and writes a replay trace with runtime/UI
  state snapshots.

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
  step edit, sequencer quick controls, sequencer playhead progression, Data
  Manager dialog flow, and overlay priority/recovery.
- `sdl/integration/run-ux-workflows.ps1` verifies each workflow exits cleanly,
  writes `trace.ndjson` and `binding-trace.ndjson`, reaches `run_end`, dispatches
  at least one binding, produces the declared capture artifacts, and enforces
  declared `# Expect:` assertions such as `playhead_progress`.
- `sdl/integration/generate-ux-report.ps1` derives a Markdown report from the
  workflow scripts, replay traces, binding traces, and BMP files. The `.ux`
  script remains the source of truth for workflow intent, timing, and capture
  names.
- `sdl/integration/workflows/sequencer-playhead.ux` proves the SDL simulator
  advances the playhead through the same standalone runtime service used by the
  firmware path. On the 2026-04-30 local run, the workflow reported
  `playing=true` with playhead values `1 -> 7 -> 0` before stop.

## SDL/Teensy Playhead Runtime Map

The playhead owner is intentionally singular:

- Teensy entry point: `main.cpp` creates `SequencerRuntimeService` and registers
  it through `StandaloneSequencerRuntimeHook`.
- SDL native and WASM entry points create the same `SequencerRuntimeService`
  with the same `StateRefs` and register it through the same hook helper.
- Runtime gate: all three entry points use the same hook to update only while
  the standalone context is active and stop when leaving it.
- UI path: `StandaloneContext` and sequencer handlers remain UI/input owners;
  they do not tick playback runtime.
- Evidence path: `UxScenarioRunner` writes `playing` and `playhead_step` into
  `trace.ndjson`; `run-ux-workflows.ps1` enforces
  `# Expect: playhead_progress` without hardcoding a specific step number.

## SDL/Teensy Feature Parity Map

| Feature lane | Teensy | SDL native | SDL WASM | Status |
|---|---|---|---|---|
| Context/handler assembly | `registerContexts(...)` | Same | Same | Aligned |
| Sequencer runtime/playhead | `SequencerRuntimeService` hook | Same | Same | Aligned |
| MIDI output/input | USB MIDI | libremidi native | libremidi WebMIDI | HAL-specific |
| Frame/control transport | USB serial frames | UDP bridge transport | WebSocket bridge transport | HAL-specific |
| Input source | Physical controls | SDL simulator mapper | SDL/browser mapper | HAL-specific |
| Persistence | SD card backends | File storage | Memory storage | Intentionally different |
| Storage recovery | SD hot-swap recovery | Not applicable | Not applicable | Teensy-only |
| UX workflow capture/report | Hardware/log review | Native replay and BMP report | Not exposed | Simulator-only |

## Teensy Semantic UX Capture

Teensy can now be built with an explicit validation-only semantic recorder:

```powershell
ms build core --target teensy --env dev_ux_recorder
```

This opt-in build defines `MS_UX_RECORDER`. Normal `dev` and `release` firmware
builds leave the recorder out of the app wiring.

The recorder is intentionally attached at the `InputBinding` dispatch layer,
not at the physical button/encoder controllers. This keeps the hardware capture
aligned with the same logical binding decisions used by SDL replay:

- only dispatched bindings are emitted by default;
- records include gesture semantics such as `press`, `long_press`, `double_tap`,
  `combo`, `turn`, and `turn_while_pressed`;
- records include stable logical names for buttons, encoders, view, and overlay;
- records include a pre-dispatch UX snapshot plus a post-update UX snapshot, so
  workflow reconstruction can see both the source context and the resulting UI
  state.
- validation-only semantic context is supplied by the active app context through
  `SemanticUxContextProvider`; `StandaloneContext` reads `CoreState` and root
  input mappings directly to emit compact business primitives such as
  `mode`, `effect`, `target`, `target_step`, `property`, `value_label`, and
  `step_on`. Sequencer step-grid/property-selector semantics come from
  `CoreState::sequencer`; global settings semantics reuse the same render-data
  formatter used by the overlay UI. Downstream tools must transport these fields
  instead of rebuilding them from logs or UI state.

Output lines are emitted through the normal firmware log path, prefixed with
`UXR ` in the message body, and contain compact JSON:

```text
UXR {"seq":1,"ms":1234,"kind":"encoder","gesture":"turn","encoder":"MACRO_1","encoder_id":301,"value_kind":"absolute","value_milli":640,"binding":42,"scope":7,"authority_scope":7,"pre_view":"sequencer","pre_overlay":"none","pre_playing":0,"pre_playhead":-1,"pre_page":0,"view":"sequencer","overlay":"none","playing":0,"playhead":-1,"page":0,"shared_track":0,"shared_mask":1,"mode":"sequencer.step_grid","effect":"edit_step_property","target":"step","target_step":0,"pre_property":"Velocity","property":"Velocity","value_label":"81","step_on":1}
```

At boot, an active recorder build also emits:

```text
UXR {"kind":"session","event":"boot","enabled":1}
```

Encoder movement carries explicit value semantics without floating-point
formatting. Relative encoders emit `value_kind:"delta"` plus signed
`delta_milli`; normalized encoders emit `value_kind:"absolute"` plus
`value_milli`. This keeps replay reconstruction honest: navigation turns remain
signed movement, while macro encoders remain absolute values or first/last
ranges instead of fake signed deltas.

The recorder uses a fixed-size pending queue and reports dropped records with a
`kind:"drop"` line. It is designed as a source log for later workflow
reconstruction; generated `.ux` timings should be synthesized from the gesture
semantics and input thresholds rather than copied from real hardware timing.

## Fragility Points To Watch

- Most LVGL widget code is built by the native target but not exercised by
  `ms test core`; the test suite mainly covers model/projection logic.
- Binding-level tracing lives in `open-control::InputBinding`; SDL only writes
  the callback stream to disk. This keeps future hardware replay aligned with
  native replay instead of duplicating dispatch knowledge in the simulator.
- Teensy semantic UX capture is deliberately validation-only. Keep output volume
  focused on dispatch records unless a specific debugging session requires
  lower-level candidate/no-dispatch traces.
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

- Review and curate the derived UX report as the first visual reference report
  for human UI review.
- Decide which visual contracts are stable enough for automated comparison.
- Add only focused assertions after manual baselines exist, for example image
  dimensions, non-empty frame, and selected scenario visibility.
- Add symbolic names for scope ids / handlers if trace review shows raw runtime
  ids are too hard to read.
- Keep render refactors scoped to places where capture evidence shows real
  fragility or where existing projection code is already duplicated.

## Exit Signal

Sprint 5 is complete for capture/replay/report plumbing and semantic runtime
telemetry. The remaining follow-up is to choose stable visual comparison
contracts from the generated report instead of expanding infrastructure.
