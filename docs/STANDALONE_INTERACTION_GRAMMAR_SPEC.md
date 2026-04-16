# Standalone Interaction Grammar Spec

> **Date**: April 16, 2026
> **Status**: Current-contract baseline
> **Scope**: Standalone main views, currently `Sequencer` and `Macro`
> **Contract**: this doc references only live state and handler seams in the current repo. If the implementation changes, this file must change in the same wave.

Historical note:

- older proposals used `LEFT_CENTER + LEFT_BOTTOM` as the structural selector entry point
- the current standalone contract uses `NAV long press` to enter structural selection and `StructureNavigationFocus` to switch between `PAGE` and `TRACK`

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

## 3. Shared Interaction Contract

### Primary Rules

- `NAV turn` = navigate the currently active context
- `NAV long press` = enter structural selection for the current `StructureNavigationFocus`
- `NAV release` = cycle focus, commit previewed structure, or toggle the current structural selection depending on mode
- `LEFT_BOTTOM hold` = inline property/clutch mode
- `LEFT_CENTER hold` = quick-controls mode
- `LEFT_TOP` = cancel the active structural or inline selection mode
- `8 encoders / 8 buttons` = direct action on the 8 visible lanes

### Mental Model

- without modifiers, the user is playing
- `LEFT_BOTTOM` changes what the lane controls edit
- `LEFT_CENTER` opens global quick controls
- structural selection is shared across views through:
  - `StructureNavigationFocus`
  - `TrackNavigationState.selection`
  - per-view page-selection state

This is the current shared grammar implemented by the standalone handlers.

---

## 4. Sequencer Current Baseline

The current Sequencer implements the shared grammar with separate handlers for:

- step editing / structure navigation
- inline step-property selection
- pattern quick controls

### Main Layer

- `NAV turn` changes page or track preview depending on `StructureNavigationFocus`
- `NAV release` cycles `PAGE` / `TRACK` focus when no selection is active
- `NAV release` creates the previewed page or track when the cursor is on an add slot
- `macro buttons` toggle the 8 visible steps on release
- `BOTTOM_LEFT` and `BOTTOM_RIGHT` provide erase/remove/copy/paste/duplicate actions for the focused structure

Implementation anchors:

- [src/handler/sequencer/SequencerStepHandler.cpp](../src/handler/sequencer/SequencerStepHandler.cpp)
- [src/state/TrackNavigationState.hpp](../src/state/TrackNavigationState.hpp)
- [src/state/sequencer/SequencerState.hpp](../src/state/sequencer/SequencerState.hpp)

### Property Selector

- `LEFT_BOTTOM` opens the inline property selector
- `NAV` changes active property
- release applies
- `LEFT_TOP` cancels and restores the previous property

Implementation anchor:

- [src/handler/sequencer/SequencerPropertySelectorHandler.cpp](../src/handler/sequencer/SequencerPropertySelectorHandler.cpp)

### Quick Controls

- `LEFT_CENTER` opens quick controls
- `NAV` selects the focused quick-control item
- `OPT` edits the focused quick-control item
- release applies
- `LEFT_TOP` cancels

Implementation anchor:

- [src/handler/sequencer/SequencerPatternQuickControlsHandler.cpp](../src/handler/sequencer/SequencerPatternQuickControlsHandler.cpp)

### Structural Selector

- `NAV long press` opens structural selection for the current focus:
  - `TRACK` focus uses `trackNavigation.selection`
  - `PAGE` focus uses `sequencer.structureUi.pageSelection`
- `NAV turn` moves the selection cursor
- `NAV release` toggles the selected candidate
- `LEFT_TOP` cancels and restores the non-selection preview state
- structural focus stays shared with the global track navigation strip and header UI

Implementation anchors:

- [src/handler/sequencer/SequencerStepHandler.cpp](../src/handler/sequencer/SequencerStepHandler.cpp)
- [src/context/standalone/StandaloneUiAssembly.cpp](../src/context/standalone/StandaloneUiAssembly.cpp)

---

## 5. Macro Current Baseline

Macro already follows the same structural grammar, but with different lane semantics.

### Main Layer

- `NAV turn` changes page or track preview depending on `StructureNavigationFocus`
- `NAV release` cycles focus, commits a previewed page change, or creates a previewed page/track
- `8 encoders` edit the active macro property for the visible page

### Clutch / Property Cycling

- `LEFT_BOTTOM` activates clutch mode
- while clutch mode is active:
  - `NAV` cycles `VALUE`, `CC`, `CHANNEL`
  - the macro encoders edit the selected property
- releasing `LEFT_BOTTOM` applies the current clutch edits and returns to `VALUE`

### Quick Controls

- `LEFT_CENTER` opens
- `NAV` changes focused item between `GLOBAL_CHANNEL` and `CC_OFFSET`
- `OPT` edits the focused item
- release applies
- `LEFT_TOP` cancels

### Structural Selector

- `NAV long press` opens structural selection for the current focus:
  - `TRACK` focus uses `trackNavigation.selection`
  - `PAGE` focus uses `macroUi.pageSelection`
- `NAV turn` moves the selection cursor
- `NAV release` toggles the selected candidate
- `LEFT_TOP` cancels the active structural selection
- page and track creation use preview add slots, not a separate structural overlay

Implementation anchors:

- [src/handler/macro/MacroPerformanceHandler.cpp](../src/handler/macro/MacroPerformanceHandler.cpp)
- [src/state/macro/MacroUiState.hpp](../src/state/macro/MacroUiState.hpp)
- [src/state/TrackNavigationState.hpp](../src/state/TrackNavigationState.hpp)

---

## 6. Shared Structural Operations

Both views currently share these structural concepts:

- `StructureNavigationFocus` chooses whether `NAV` is operating on `PAGE` or `TRACK`
- preview state uses:
  - `trackNavigation.previewTrackIndex`
  - `trackNavigation.previewAddSlot`
  - view-specific page preview state
- selection state uses:
  - `trackNavigation.selection`
  - `macroUi.pageSelection` or `sequencer.structureUi.pageSelection`
- bottom actions act on the currently focused structure:
  - `erase`
  - `remove`
  - `copy`
  - `paste`
  - `duplicate` when selection is active

This is important for maintainability: the shared grammar now lives in state seams, not in one old selector handler.

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

## 8. Maintenance Rule

If the standalone interaction grammar changes:

1. update this file
2. update the relevant handler/state references in the same change
3. update [CORE_ARCHITECTURE_AUDIT_2026_04.md](CORE_ARCHITECTURE_AUDIT_2026_04.md) if the change affects ownership, naming, or placement rules

---

## 9. Non-Goals

This spec does not propose:

- immediate unification of every internal state struct between Macro and Sequencer
- removal of valid feature-specific workflows that still need separate handlers
- forced reuse of Sequencer widgets where Macro semantics differ
- speculative selector concepts that do not exist in the live code

The target is a common interaction language grounded in the current implementation, not a visual clone.
