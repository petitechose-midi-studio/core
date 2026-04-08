# Standalone Interaction Grammar Spec

> **Date**: April 8, 2026  
> **Status**: Proposed baseline interaction grammar  
> **Scope**: Standalone main views, initially `Sequencer` and `Macro`

---

## 1. Goal

Define one shared interaction grammar across standalone main views so the device behaves like one instrument, not a collection of unrelated screens.

This grammar must satisfy four constraints:

- the most-used actions stay available without overlays
- global context actions are distinct from per-lane actions
- transient modes are visible, cancelable, and reversible
- view layouts reserve the same structural zones by default

---

## 2. Shared Spatial Frame

Main views should share the same structural frame:

- `header`
- `interaction row`
- `left function strip`
- `center content zone`
- `right property strip`
- `bottom action strip`

Rules:

1. these zones are always reserved, even when a strip has no active content
2. the center content zone always fills the remaining space
3. the view-specific content fills the center zone
4. centering belongs to widgets inside cells, not to the full content area
5. view code should not rely on ad hoc offsets or fixed positioning corrections

This is now reflected in the shared frame helper used by `MacroView` and `SequencerView`.

---

## 3. Shared Gesture Grammar

### Primary Rules

- `NAV turn` = navigate the primary context
- `NAV press` = act on the currently focused or selected target
- `LEFT_BOTTOM` = property-selection mode
- `LEFT_CENTER` = quick controls mode
- `LEFT_CENTER + LEFT_BOTTOM` = structural selector mode
- `LEFT_TOP` = cancel / restore snapshot
- `8 encoders / 8 buttons` = direct action on the 8 visible lanes

### Mental Model

- without modifiers, the user is playing
- `LEFT_BOTTOM` changes what lane gestures mean
- `LEFT_CENTER` changes global context parameters
- `LEFT_CENTER + LEFT_BOTTOM` changes the current structural target

This separation is the core of the instrument-like workflow.

---

## 4. Sequencer Baseline

The current Sequencer already implements most of this grammar.

### Main Layer

- `NAV turn` changes page
- `NAV press` toggles the focused step
- `macro buttons` toggle visible steps

Reference:

- [SequencerStepHandler.cpp](/C:/Users/miu-lab/ms-dev-env/midi-studio/core/src/handler/sequencer/SequencerStepHandler.cpp)

### Property Selector

- `LEFT_BOTTOM` opens the inline property selector
- `NAV` changes active property
- release applies
- `LEFT_TOP` cancels and restores the previous property

Reference:

- [SequencerPropertySelectorHandler.cpp](/C:/Users/miu-lab/ms-dev-env/midi-studio/core/src/handler/sequencer/SequencerPropertySelectorHandler.cpp)

### Quick Controls

- `LEFT_CENTER` opens quick controls
- `NAV` selects the focused quick-control item
- `OPT` edits the focused quick-control item
- release applies
- `LEFT_TOP` cancels

Reference:

- [SequencerPatternQuickControlsHandler.cpp](/C:/Users/miu-lab/ms-dev-env/midi-studio/core/src/handler/sequencer/SequencerPatternQuickControlsHandler.cpp)

### Structural Selector

- `LEFT_CENTER + LEFT_BOTTOM` opens track selection
- `NAV` selects a candidate track
- `NAV press` toggles enabled state of the selected track
- releasing the combo applies the selected track
- `LEFT_TOP` cancels and restores the snapshot

Reference:

- [SequencerTrackSelectorHandler.cpp](/C:/Users/miu-lab/ms-dev-env/midi-studio/core/src/handler/sequencer/SequencerTrackSelectorHandler.cpp)

---

## 5. Macro Target Grammar

Macro should converge to the same structure.

### Main Layer

- `NAV turn` changes Macro page
- `8 encoders` edit macro value directly
- `8 buttons` keep their direct lane meaning or remain unassigned until a safe meaning exists

### Property Selector

- `LEFT_BOTTOM` opens the Macro property selector
- `NAV` selects one of:
  - `Value`
  - `CC`
  - `Channel`
- release applies
- `LEFT_TOP` cancels and restores the previous property

This is already close to the current `MacroPerformanceHandler` model.

### Quick Controls

Macro quick controls should be global, not per-lane.

Recommended v1 items:

- `Channel Global`
- `CC Offset Global`

Interaction should match Sequencer quick controls:

- `LEFT_CENTER` opens
- `NAV` changes focused item
- `OPT` edits focused item
- release applies
- `LEFT_TOP` cancels

### Structural Selector

Macro structural selection should mirror Sequencer track selection.

Recommended mapping:

- `LEFT_CENTER + LEFT_BOTTOM` opens the Macro page selector
- `NAV` selects a page candidate
- releasing the combo applies the selected page
- `LEFT_TOP` cancels and restores the snapshot

`NAV press` should remain unassigned in this selector unless a clear, low-risk page-level action is identified.

---

## 6. Global Channel and CC Offset

### Global Channel

The working assumption is that many real use cases want one page of macros to target one device context.

So the recommended model is:

- each Macro page has a `global channel`
- per-macro channel remains possible as an override only if needed later
- the default editing path should bias toward global channel editing

This reduces repetitive remapping and fits the intended musical workflow better.

### CC Offset Global

The recommended model is an offset-style transformation, parallel to Sequencer pattern offset:

- quick controls keep a snapshot of the page mapping state
- editing `CC Offset Global` shifts the page's CC bank relative to that snapshot
- cancel restores the snapshot
- apply commits the transformed bank

This should be treated as a bank-level operation, not as a destructive remap action.

---

## 7. Header Behavior

The header should remain structurally aligned across views.

Shared principles:

- more breathing room on the top row for `Page` / `Track` titles
- left accent remains context-specific
- right-side activity indicators stay visible
- Sequencer keeps the page-progress strip
- Macro keeps a single-row neutral header without Sequencer progress semantics

Current adjustment:

- header top-row height increased slightly in both views to improve title readability

---

## 8. Implementation Order

Recommended order:

1. stabilize shared frame geometry across views
2. finalize the current Macro property selector behavior
3. increase header breathing room in Macro and Sequencer
4. add Macro quick controls with `Channel Global` and `CC Offset Global`
5. add Macro structural page selector using the Sequencer track-selector grammar
6. revisit whether `NAV press` in Macro structural mode needs a meaning

---

## 9. Non-Goals

This spec does not propose:

- immediate unification of all overlay internals
- removal of deep Macro edit overlays
- forced reuse of Sequencer-specific widgets for Macro content
- destructive remapping shortcuts without snapshot/cancel semantics

The target is a common interaction language, not a visual clone.
