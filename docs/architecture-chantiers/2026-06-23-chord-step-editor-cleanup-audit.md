# Chord Step Editor Cleanup Audit - 2026-06-23

Purpose: keep a running, source-backed audit of the chord step editor work and
its adjacent code paths before committing the feature. The goal is a codebase
that remains readable, maintainable, deterministic, and efficient on Teensy.

## Scope

- `midi-studio/core` branch `feature/chord-step-editor-ui`.
- `open-control/note` chord model changes consumed by Core.
- Adjacent Core paths touched by the feature: sequencer state, content view,
  handlers, overlay presenters, UI renderers, workflows, tests, and generated
  icon assets.

## Review Rules

- Prefer one explicit domain path over generic helpers that hide state changes.
- UI stays projection/render only; handlers and state/domain ops own mutation.
- Do not keep legacy compatibility branches unless they are required by current
  product needs or an explicit migration policy.
- Avoid RAM1 pressure: cold constants should be `constexpr`/flash-friendly;
  warm or large mutable buffers need a clear owner and should not be duplicated.
- LVGL render paths should use cached props/revisions and local invalidation;
  full layout recalculation should be structural, not per-value.
- New abstractions are acceptable only when they remove real duplication while
  preserving domain meaning.

## Running Findings

### Resolved During This Audit

- Removed dead chord preview context UI state and formatter data from the step
  editor overlay path.
- Centralized sequencer interaction action icon selection in
  `SequencerActionStripVisuals.hpp`.
- Removed redundant scale-revision subscriptions from selector presenters rather
  than increasing signal subscriber capacity.
- Reduced repeated layout work in `SequencerStepEditOverlay`: layout updates now
  happen only on first show or structural visibility changes.
- Reduced redundant LVGL style writes for chord preview map color.
- Narrowed new chord UI headers with forward declarations where possible.
- Removed duplicate MIDI clamping in `open-control/note` chord code.
- Fixed child chord detail editing so local edits start from the effective
  inherited chord spec, not from default local spec.
- Removed the inline sequencer-envelope `StepNodeRecordV2` compatibility path.
  Current codecs now accept only the current envelope layout; old layouts must
  go through the explicit project migration layer instead of remaining hidden
  inside the hot/current decode path.
- Extracted chord step-editor mechanics from `SequencerStepEditHandler.cpp` into
  `SequencerChordEditOps.*`. The overlay handler now routes input; chord choice
  mapping, normalized-value conversion, localizing edits, and reset behavior live
  behind one focused chord-edit module.
- Added per-voice render caching in `SequencerStepEditOverlay` so chord preview
  markers only update LVGL size/position/color/opacity when their resolved
  marker state actually changes.
- Added dedicated chord field value buffers to `StepEditRenderData` instead of
  reusing unrelated step-editor row buffers. This removes an implicit coupling
  between chord detail UI and row ordering.
- Regenerated `test/fixtures/projects/v1_1/current-from-stale-sequencer.mspj`
  with the current file tool after removing inline envelope-v2 support. The
  stale v1.0 fixture still proves unsupported-old-sequencer behavior; the v1.1
  fixture is now a true current-layout project again.
- Renamed `SequencerInteractionScope::TRACK_LEGACY` to `TRACK` and updated the
  UX trace label from `sequencer.track_legacy` to `sequencer.track`. This keeps
  the current transition state out of the runtime vocabulary.
- Simplified CoreSettings version handling: only the current settings layout is
  accepted. Any stale settings version now resets and rewrites defaults instead
  of preserving old shortcut offsets.
- Moved the static chord-resolution lookup tables in `open-control/note` to
  `PROGMEM`. Firmware symbol inspection now reports them as read-only (`r`)
  symbols instead of writable data, avoiding unnecessary RAM1 pressure for cold
  deterministic lookup data.
- Fixed direct PlatformIO Teensy builds by removing `FLASHMEM` from
  `open-control/framework`'s `ExclusiveVisibilityStack` template methods.
  Teensy/GCC 15 emits inline template instantiations in COMDAT sections, which
  conflicts with regular functions when both are forced into the shared
  `.flashmem` section. This keeps direct `pio run` and `ms build` on one
  functional path.
- Audited `STEP_EDITOR` context-row actions against the interaction policy and
  handler tests. Child context deletion intentionally remains a hold action
  (`REMOVE_STEP_EDITOR_CONTEXT`) while short press is reserved for value-row
  reset/copy workflows; this keeps nested-content destruction behind the same
  timeout guard used elsewhere.
- Reduced `StepContentBadgeProjection.hpp` include weight by forward-declaring
  chord telemetry types and moving the full chord/runtime includes to the `.cpp`.
  This keeps the grid badge API lightweight and avoids spreading runtime-state
  dependencies through UI render headers.

### Finalization Notes

- Generated font churn is expected and explained by the new chord icons under
  `asset/icon`. The source SVGs, regenerated font file, and generated C++ font
  data must stay committed together.
- The new UX workflows, handler/unit tests, README notes, and this audit are
  final feature artifacts, not scratch files.

### Follow-Up Items

- Future sequencer-envelope migrations should be added to
  `ProjectChunkMigration`/`ProjectMigration` when backwards compatibility
  becomes a product requirement. Do not reintroduce multi-layout current codecs
  in the hot/current decode path for this branch.

## Validation Log

- `ms test open-control-note`: OK `7/7`.
- `ms test open-control-framework`: OK `22/22`.
- `ms test core`: OK `69/69`.
- `ms ux run core --select sequencer/editing/step-edit-chord.ux`: OK.
- `ms ux run core --select sequencer/editing/step-edit-chord-strum.ux`: OK.
- `ms build core --target teensy --env dev`: OK.
- Final observed memory: FLASH `811KB/7.8MB`, RAM1 `392KB/512KB`, RAM2
  `242KB/512KB`, PSRAM `4182KB/8MB`.
- `arm-none-eabi-nm -S --size-sort .pio/build/dev/firmware.elf` confirms
  `SPREAD_OCTAVE_SHIFTS`, `VARIANT_INTERVAL_PICK`,
  `CHORD_QUALITY_PATTERNS`, `DEGREE_FAMILY_INTERVALS`, and
  `CHROMATIC_FAMILY_INTERVALS` are read-only (`r`) symbols.
- Direct `pio run` from `midi-studio/core`: OK after the
  `ExclusiveVisibilityStack` template section fix. PlatformIO reported FLASH
  code/data `615532/165968`, RAM1 variables/code/padding `63328/303896/23784`,
  RAM2 variables `247968`, EXTRAM variables `4282368`.
- `git diff --check`: OK for `midi-studio/core`, `open-control/note`, and
  `open-control/framework` (line-ending warnings only on generated/font files).
