# Sprint 3: Shared Structure Mechanics Recognition

Updated: 2026-04-29

Purpose: define the precise Sprint 3 scope before changing shared page/track
structure behavior.

Current status: complete for the scoped Sprint 3 tranche. No behavior change was
needed. The current code has a reasonable ownership split, direct structure
coverage, and one narrow sequencer-only extraction for track creation.

## Scope

Sprint 3 is about shared structure mechanics across macro performance and
sequencer step editing.

Included:

- Page and track navigation focus.
- Add-slot preview and creation.
- Selection state, selected masks, and cursor movement.
- Erase, remove/delete, duplicate, copy, and paste behavior.
- Shared track mask/active-track invariants used by macro and sequencer.
- Test coverage for reusable slot primitives and handler-level structure flows.

Excluded:

- Rewriting macro and sequencer structure workflows into one generic workflow.
- Changing persistence format or migration policy.
- Visual LVGL/SDL validation.
- Hardware timing or realtime MIDI proof.
- Input/overlay modal conflict work already covered by Sprint 2.

## Current Source Checks

Run from `midi-studio/core` or anywhere in the workspace:

```powershell
ms test core
rg -n "StructureSlotOps|MacroStructure|SequencerStructure|SharedTrack" src test docs/architecture-chantiers
rg -n "duplicateSelection|copyCurrentStructure|pasteCurrentStructure|removeSelected|nextNavigationTarget" src test
```

Current local evidence:

- The Sprint 3 behavior changes in this tranche are test-only; production code
  only removes duplicate sequencer track-creation mechanics.
- Recent structure-related commits are `7421559`, `924c7f4`, `8842d33`,
  `93a35d2`, `9b3b762`, and `a37dc58`.
- `ms test core` passes `44/44` after adding Sprint 3 structure coverage.
- `ms` is the documented entry point for native unit tests. Do not document
  `uv run ms ...` for this workflow.

## Ownership Matrix

| Surface | Current owner | Sprint 3 decision |
|---|---|---|
| Slot-mask primitives | `src/state/shared/StructureSlotOps.hpp` | Shared primitive boundary. Add direct tests before relying on it as contract. |
| Selection state | `src/state/StructureSelectionState.hpp`, `src/state/TrackNavigationState.hpp` | Shared transient state is appropriate; keep domain mutations outside it. |
| Clipboard payload kinds | `src/state/StructureClipboardState.hpp` | Shared transport container only; macro and sequencer payload semantics stay domain-owned. |
| Shared track invariant | `SharedTrackCoordinator`, `SharedTrackDomainServices` | Already Sprint 1 authority. Sprint 3 should use it, not bypass it. |
| Macro page/track structure mutations | `MacroStructureDomainServices` | Domain-owned because it touches macro pages, page names, runtime sync, config revision, and persistence. |
| Macro structure navigation/edit flow | `MacroStructureWorkflow` through `MacroPerformanceHandler` | Domain workflow. Shared helpers are fine; do not collapse with sequencer flow. |
| Sequencer page/track navigation | `SequencerStructureNavigationWorkflow` | Domain workflow because page count, focused step, and active track editor rules are sequencer-specific. |
| Sequencer structure edits | `SequencerStructureEditWorkflow`, `SequencerSnapshotOps`, `SequencerTrackBankOps` | Domain-owned because edits shift step payloads, snapshots, active editor state, and track-bank state. |
| Sequencer track creation | `SequencerStructureTrackOps.hpp` | Narrow shared helper for the two sequencer workflows only; it does not mix macro and sequencer domains. |
| Handler input bindings | `MacroPerformanceHandler`, `SequencerStepHandler` | Already covered by Sprint 2 state-machine recognition; Sprint 3 tests should target behavior, not rebalance input ownership. |

## Operation Classification

| Operation | Shared mechanic | Macro-specific behavior | Sequencer-specific behavior |
|---|---|---|---|
| Move enabled slot | `wrapIndex`, `nextEnabledIndex`, `nextNavigationTarget` | Pages use enabled masks and terminal add slot. Tracks switch active macro track when landing on enabled track. | Pages map to visible page/focused step. Tracks switch active sequencer editor through shared-track services. |
| Preview add slot | Shared concept only | Page add slot is next slot after highest enabled page; track add slot can land on disabled track slots. | Page add slot is based on active page count; track add slot uses track-bank slot availability. |
| Enter/cancel selection | `StructureSelectionState` | Selection cursor syncs to active macro page/track and clears preview slots. | Page selection must normalize add-slot preview back to an existing page. |
| Delete current | `removeIndex` | Applies macro page/track mask mutation, presentation refresh, config revision, and persist request. | Track deletion applies shared-track state; page deletion removes/shifts sequencer page payloads. |
| Delete selection | `removeSelected` for masks | Deletes selected macro pages/tracks through macro domain services. | Track selection uses `removeSelected`; page selection removes pages from the end to preserve indexes. |
| Duplicate selection | `duplicateSelectionIntoFreeSlots` for mask slots | Copies macro page/track data into free slots and selects first duplicate. | Tracks copy persistent track state; pages duplicate sequencer pages and shift payloads. |
| Copy/paste page | Shared clipboard container | Copies/pastes `MacroPageData`, enables target page, refreshes runtime/persistence. | Copies/pastes page step payloads and adjusts length/revision/focused step. |
| Copy/paste track | Shared clipboard container | Copies/pastes `MacroTrackData`, enables target shared track, refreshes macro state. | Copies/pastes `SequencerPatternSnapshot`, stores active track, and refreshes track-bank state. |
| Erase current | No shared primitive | Resets macro page/track data without removing the slot. | Clears page step range or resets active sequencer track without changing enabled mask. |

## Current Coverage

Already covered:

- Shared slot primitives through `test_StructureSlotOps`.
- Macro page/track selection deletion through `test_MacroPerformanceHandler`.
- Macro selected page/track duplicate flows through
  `test_MacroPerformanceHandler`.
- Macro page copy and long-press paste through `test_MacroPerformanceHandler`.
- Macro track copy and long-press paste through `test_MacroPerformanceHandler`.
- Macro add-slot/page and track navigation behavior through
  `test_MacroPerformanceHandler`.
- Sequencer page/track selection deletion through `test_SequencerStepHandler`.
- Sequencer selected page/track duplicate flows through
  `test_SequencerStepHandler`.
- Sequencer page copy and long-press paste through `test_SequencerStepHandler`.
- Sequencer track copy and long-press paste through `test_SequencerStepHandler`.
- Sequencer add-slot creation and deleted track slot recreation through
  `test_SequencerStepHandler`.
- Shared track sanitization and macro/sequencer synchronization through
  `test_SharedTrackCoordinator` and `test_SharedTrackDomainServices`.
- Sequencer page/snapshot primitives through `test_SequencerSnapshotOps`.

Remaining watchpoint:

- Boundary behavior when duplicate/paste targets are full or selected masks
  would delete every enabled slot is partly covered at the slot primitive layer;
  add handler-level cases only if future behavior changes touch those paths.

## Recommended Sprint 3 Plan

1. Completed: add direct native tests for `StructureSlotOps.hpp`.
   Cover `removeIndex`, `removeSelected`, `duplicateSelectionIntoFreeSlots`,
   `nextEnabledIndex`, `nextAddIndexAfterHighest`, and `nextNavigationTarget`.

2. Completed: add missing handler regressions without changing behavior.
   Focus on duplicate selection and track copy/paste for both macro and
   sequencer.

3. Completed: re-run `ms test core` and inspect failures before refactoring.
   Result: `44/44`; one sequencer page-duplicate test expectation was corrected
   to the existing append-and-focus-duplicate contract.

4. Completed: extract the only justified production duplicate.
   `SequencerStructureTrackOps.hpp` now owns the common sequencer track creation
   path used by navigation creation and edit paste-to-add-slot. No
   macro/sequencer generic workflow was introduced.

## Exit Criteria

Sprint 3 can close when:

- `StructureSlotOps` has direct native coverage. Done in `test_StructureSlotOps`.
- Missing duplicate and track copy/paste regressions are covered. Done in
  `test_MacroPerformanceHandler` and `test_SequencerStepHandler`.
- `ms test core` passes. Current result: `44/44`.
- The ownership matrix above remains true after any patch.
- No new broad `CoreState&` or direct shared-track mutation path is introduced.
- The docs state clearly which mechanics are shared primitives and which are
  macro/sequencer domain behavior.

Closure note:

- Sprint 3 is complete for this tranche. Future work should be driven by new
  behavior changes or failing regressions, not by a broad structural rewrite.
