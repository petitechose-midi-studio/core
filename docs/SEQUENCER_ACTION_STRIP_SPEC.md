# Sequencer Action Strip Spec

> **Date**: March 2026
> **Status**: Proposed interaction and UI spec
> **Scope**: Bottom action-strip system for Sequencer view and range-based copy/erase workflows

---

## 1. Goal

Add a persistent bottom action strip to the Sequencer UI so that physical button actions are always visible on screen.

This strip should:

- stay aligned with the physical button positions
- remain present in the sequencer idle state
- switch content when an inline workflow or overlay is active
- make destructive actions explicit and guarded
- reuse a stable visual grammar across idle, copy, paste, and erase flows

This document intentionally treats the action strip as part of the instrument workflow, not as decorative HUD.

---

## 2. Core Interaction Rules

These rules are now the intended baseline for sequencer bottom-button workflows.

1. `short press` enters a visible, cancelable mode.
2. `long press` enters the same operation family with a smart prefilled scope.
3. destructive actions never execute immediately from an accidental tap.
4. `LEFT_TOP` is the universal cancel action while a transient mode is active.
5. `NAV` is used to place or validate selection markers.
6. left-side actions should stay biased toward destructive or utility actions.
7. right-side actions should stay biased toward constructive actions.

This removes dependence on double-tap timing for core edit workflows.

---

## 3. Strip Model

### Fixed Slot Layout

The bottom strip should use fixed slots aligned to physical controls. The preferred slot model is:

- `LT`
- `BL`
- `NAV`
- `BR`
- `RT`

If a slot has no meaningful action in the current mode, it should remain empty or strongly dimmed rather than repurposed unpredictably.

### Visual Semantics

Each slot communicates three things:

- action family
- current availability
- current interaction phase

Recommended styling grammar:

- neutral / navigation: off-white
- destructive / erase: orange-red
- copy source: cyan
- paste target: green-cyan or acid green
- cancel: cool gray

Recommended state grammar:

- `disabled`: nearly invisible
- `dim`: available but not primary
- `focus`: currently active in the running mode
- `armed`: ready for explicit confirmation

### Cursor Color Coupling

The step-grid cursors and the strip must share the same color family:

- erase range cursor = destructive color
- copy source cursor = copy color
- paste destination cursor = paste color

This is required so the strip and the grid read as one workflow.

---

## 4. Sequencer Workflow Grammar

### Bottom Left

`BL` belongs to the erase family.

- `BL short`: open manual erase-range selection
- `BL long`: open erase mode with a prefilled range

The current recommended prefilled range for v1 is:

- full current page

This keeps the long-press behavior fast without making it dangerous. The user still sees the range and must explicitly confirm the erase.

### Bottom Right

`BR` belongs to the copy family.

- `BR short`: open manual copy-range selection
- `BR long`: preload a copy source from the full current page, then move into paste placement

The current recommended prefilled copy scope for v1 is:

- full current page

### Left Top

`LT` is always cancel while a transient strip-driven mode is active.

It should:

- cancel the current range workflow
- discard uncommitted source or destination placement
- restore the idle sequencer strip immediately

### NAV

`NAV` is the placement and validation control inside strip-driven workflows.

It should:

- place source start
- place source end
- place paste destination
- continue to act as the explicit selection marker validator

`NAV` should not commit destructive actions directly. Final destructive commit should stay on `BL`.

---

## 5. Action Strip Presets

This section defines the intended preset table for the bottom strip.

## Idle Sequencer

- `LT`: existing view-level action or hidden if not needed
- `BL`: `erase`
- `NAV`: hidden or neutral, depending on existing sequencer semantics
- `BR`: `copy`
- `RT`: existing sequencer-specific action or hidden

Notes:

- the strip is always visible in idle
- `BL` and `BR` advertise the edit families even before the user presses anything

## Erase Range: Select Start

- `LT`: `cancel`
- `BL`: `erase` in `focus`
- `NAV`: `set start`
- `BR`: `disabled`
- `RT`: `disabled`

Notes:

- entering this mode does not erase anything
- the destructive family color should already be visible

## Erase Range: Select End

- `LT`: `cancel`
- `BL`: `erase` in `focus`
- `NAV`: `set end`
- `BR`: `disabled`
- `RT`: `disabled`

## Erase Range: Armed

- `LT`: `cancel`
- `BL`: `apply erase` in `armed`
- `NAV`: `adjust range`
- `BR`: `disabled`
- `RT`: `disabled`

Notes:

- the range is visible on the grid
- pressing `BL` applies the erase to the selected range only
- pressing `LT` cancels without side effects

## Copy Range: Select Start

- `LT`: `cancel`
- `BL`: `disabled`
- `NAV`: `set start`
- `BR`: `copy` in `focus`
- `RT`: `disabled`

## Copy Range: Select End

- `LT`: `cancel`
- `BL`: `disabled`
- `NAV`: `set end`
- `BR`: `copy` in `focus`
- `RT`: `disabled`

## Paste Placement

- `LT`: `cancel`
- `BL`: `disabled`
- `NAV`: `place target`
- `BR`: `apply paste` in `armed`
- `RT`: `disabled`

Notes:

- source selection is already captured before this state
- repeated paste can later be supported by keeping the clipboard alive after the first paste

---

## 6. Long-Press Behavior

The long press should not introduce a different grammar. It should accelerate the same grammar.

### `BL long`

Recommended behavior:

1. enter erase mode
2. prefill the selected range to the full current page
3. show the `Erase Range: Armed` preset immediately
4. allow `NAV` to adjust the prefilled range before commit if desired

This means the long press is fast, visible, and still safe.

### `BR long`

Recommended behavior:

1. copy the full current page into the transient clipboard
2. immediately switch to paste placement
3. show the `Paste Placement` preset

This gives a fast whole-page duplication flow without needing double-tap timing.

---

## 7. Range Selection Behavior

The range selector is a lightweight marker independent from the current pattern playback head.

It should:

- move with `NAV`
- be able to target any step relevant to the current editing scope
- remain visually distinct from the playhead
- use mode-specific color to indicate whether the user is defining erase, copy, or paste

Range selection flow:

1. enter selection mode
2. place start marker with `NAV`
3. place end marker with `NAV`
4. transition to `armed` erase or to paste placement depending on operation

Paste flow:

1. source is already known
2. destination cursor appears
3. `NAV` sets destination anchor
4. `BR` confirms paste

---

## 8. Icon Families Needed

The action-strip system needs a stable icon set, not one-off artwork per screen.

Required icon families:

- `cancel`
- `erase`
- `erase_confirm`
- `copy`
- `paste`
- `marker_start`
- `marker_end`
- `target_place`

Optional later additions:

- `duplicate_page`
- `duplicate_pattern`
- `scope_page`
- `scope_pattern`

The current spec does not require a separate icon for every substate if color and slot state already communicate phase clearly.

---

## 9. Rendering Guidance

The strip should be implemented as fixed-position slots, not as a separate hand-authored bitmap per state.

Preferred implementation direction:

- one dedicated sequencer bottom-strip component
- fixed slot geometry
- icon atlas or icon font glyphs
- state-driven preset rendering

This fits the existing architecture better than screen-specific baked strips and keeps the workflow maintainable.

Relevant current UI anchors:

- `src/ui/view/SequencerView.cpp`
- `src/ui/sequencer/StepPropertyStrip.hpp`
- `src/ui/sequencer/PatternQuickControls.hpp`
- `src/ui/transportbar/ContextSoftkeyBar.hpp`

Likely follow-up additions:

- a dedicated bottom action-strip widget for sequencer
- sequencer UI state for range-selection and clipboard workflow phases
- a handler dedicated to strip-driven copy/erase interactions

---

## 10. What Is Decided vs. Open

### Decided

- the sequencer should have a persistent bottom action strip in idle
- the same strip system should be reused by overlays or transient inline modes
- `short press` should open manual selection workflows
- `long press` should accelerate the same workflow with a prefilled default scope
- destructive actions must remain explicitly confirmable
- `LEFT_TOP` is the cancel action inside these workflows

### Still Open

- whether `RT` gets a dedicated sequencer function in the same strip family
- whether a future full-pattern view should let long-press prefill the visible scope instead of the current page
- whether clipboard persistence after paste should be one-shot or sticky by default
- whether whole-pattern duplication needs its own dedicated shortcut later

For v1, the strip spec should not block on those open questions.

