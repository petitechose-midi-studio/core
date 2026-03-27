# Sequencer Inline Workflow Roadmap

> **Date**: March 2026
> **Status**: Approved implementation roadmap
> **Scope**: Sequencer workflow refactor for inline editing, reduced overlays, and maintainable input architecture

---

## 1. Goal

Refactor the sequencer workflow so that frequent actions become inline and instrument-like:

- `LEFT_BOTTOM` selects the step property inline and edits it immediately while held
- `LEFT_CENTER` exposes inline pattern quick controls instead of opening a full overlay
- overlays remain available only for advanced or secondary actions
- the codebase stays componentized and maintainable

This roadmap is intentionally biased toward:

- low-latency interaction
- minimal modal UI
- explicit state ownership
- reusable UI/handler boundaries

---

## 2. Feasibility Assessment

### Summary

This refactor is feasible with the current architecture.

The current codebase already provides most of the required building blocks:

- state-driven UI via `Signal`
- scoped input bindings
- isolated `SequencerView` subcomponents
- extracted `StepPropertyStrip` and `StepGrid`
- centralized sequencer encoder sync in `StandaloneContext`

The main work is not rendering. It is ownership and interaction design:

- remove inline modes from the overlay lifecycle
- add dedicated inline state
- split handlers so each gesture has a single responsibility
- keep encoder sync aligned with inline modes

### Main Constraint

The current overlay model is not the right abstraction for held inline interactions.

Today, anything treated as an overlay affects authority and encoder sync in ways that are correct for dialogs, but wrong for instrument-like temporary modes.

Therefore:

- `LEFT_BOTTOM` inline property selection must no longer be treated as an overlay
- `LEFT_CENTER` inline pattern quick controls must also not be treated as an overlay

This is the main architectural prerequisite.

---

## 3. UX Target

### Step Property Mode

When the user holds `LEFT_BOTTOM`:

- a small inline cursor appears under the property strip
- `NAV` moves the highlight
- the highlighted property becomes the active property immediately
- the 8 macro encoders and `OPT` edit that property immediately
- releasing `LEFT_BOTTOM` keeps the current property selected
- pressing `LEFT_TOP` while held restores the snapshot and exits

There is no passive preview state. Highlighted means active.

### Pattern Quick Controls

When the user holds `LEFT_CENTER`:

- an inline quick-control strip is active in the top area
- items are `CH`, `DIV`, `LEN`
- `NAV` moves between the three items
- `OPT` edits the focused item immediately
- releasing `LEFT_CENTER` exits while keeping the last focused item
- pressing `NAV` opens an advanced overlay for the focused item if needed
- pressing `LEFT_TOP` while held restores the snapshot and exits

Important:

- do not call this `TRACK` yet
- the current engine exposes a single sequencer with `midiChannel`, not a true multi-track model
- the correct short label is `CH`

---

## 4. Design Principles

These rules should guide the refactor.

1. Inline frequent actions, overlay infrequent actions.
2. Held modes must not be modeled as modal overlays.
3. Views render only from state.
4. Handlers own interaction logic and state transitions.
5. Encoder mapping logic must be centralized, not duplicated in views.
6. The top bar must communicate context without reintroducing modal UI complexity.
7. Avoid vocabulary that promises unsupported engine features.

---

## 5. Target Architecture

### UI Components

#### `SequencerView`

Role:

- assemble subcomponents
- manage render dirtiness
- remain a thin composition layer

Must contain:

- `SequencerHeaderBar`
- `PatternQuickControls`
- `StepPropertyStrip`
- `StepGrid`

#### `PatternQuickControls`

New component.

Role:

- render `CH / DIV / LEN`
- render active item and inline selection state
- expose a lightweight scope element if needed
- remain view-only

Suggested files:

- `src/ui/sequencer/PatternQuickControls.hpp`
- `src/ui/sequencer/PatternQuickControls.cpp`

#### `StepPropertyStrip`

Existing component.

Role after refactor:

- render active property
- render transient selection cursor while `LEFT_BOTTOM` is held
- reflect live highlighted property
- remain view-only

#### `StepGrid`

Existing component.

Role:

- keep rendering focused on step content and overlays
- remain independent from interaction policy

No workflow logic should move into `StepGrid`.

---

## 6. State Model

The refactor needs dedicated inline state instead of reusing overlay state.

### New Inline States in `SequencerState`

Suggested additions in [SequencerState.hpp](../src/state/sequencer/SequencerState.hpp):

#### `StepPropertyInlineSelectorState`

- `Signal<bool> selecting`
- `Signal<int> selectedIndex`
- `int snapshotIndex`
- `bool snapshotValid`

Purpose:

- model `LEFT_BOTTOM` hold interaction
- support cancel/restore without modal overlay machinery

#### `PatternQuickControlState`

- `Signal<bool> selecting`
- `Signal<uint8_t> focusedItem`
- `uint8_t snapshotLength`
- `uint8_t snapshotStepsPerBeat`
- `uint8_t snapshotMidiChannel`
- `bool snapshotValid`

Purpose:

- model `LEFT_CENTER` hold interaction
- support inline editing and optional cancel

### Existing Overlay States

Current states to reduce or repurpose:

- `SequencerPropertySelectorOverlayState`
- `SequencerPatternConfigOverlayState`

Recommended path:

- remove `SequencerPropertySelectorOverlayState` from normal use
- keep `SequencerPatternConfigOverlayState` only if an advanced overlay remains useful

---

## 7. Input Handling Plan

### 7.1 Step Property Inline Mode

Replace the current overlay-flavored property selector handler with a real inline mode handler.

Suggested replacement:

- `src/handler/sequencer/SequencerStepPropertyModeHandler.hpp`
- `src/handler/sequencer/SequencerStepPropertyModeHandler.cpp`

Responsibilities:

- handle `LEFT_BOTTOM` press/release
- take and restore snapshots
- move `selectedIndex` on `NAV`
- update `activeStepProperty` immediately during navigation
- never go through `OverlayManager`

State transition model:

- `press LEFT_BOTTOM` -> enter selecting mode
- `NAV turn` -> highlight next property and set `activeStepProperty`
- `release LEFT_BOTTOM` -> exit selecting mode
- `LEFT_TOP` while selecting -> restore snapshot and exit

### 7.2 Pattern Quick Control Inline Mode

Create a dedicated handler.

Suggested files:

- `src/handler/sequencer/SequencerPatternQuickControlHandler.hpp`
- `src/handler/sequencer/SequencerPatternQuickControlHandler.cpp`

Responsibilities:

- handle `LEFT_CENTER` press/release
- move `focusedItem` across `CH / DIV / LEN`
- edit the focused item with `OPT`
- optionally open advanced overlay with `NAV` press
- restore snapshots on cancel
- never use `OverlayManager` for the inline mode itself

### 7.3 Advanced Overlay Behavior

Advanced overlays can remain, but only as secondary detail views:

- `NAV press` on `CH` could open a detailed channel selector
- `NAV press` on `DIV` could open a richer discrete selector if needed
- `NAV press` on `LEN` could open a range/detail editor if needed

This keeps overlays as explicit drill-down, not as the default workflow.

---

## 8. Encoder Sync Implications

This is the most important technical constraint after state ownership.

Today, sequencer encoder sync is centralized in [StandaloneContext.cpp](../src/context/StandaloneContext.cpp).

Current behavior:

- encoder configs and positions are resynced from state
- resync is deferred when overlays are visible

That is correct for dialogs, but wrong for inline held modes.

### Required Change

Refactor sequencer encoder sync so it distinguishes:

- blocking overlays
- inline selection/editing modes

Inline modes must not be treated as blocking overlays.

Otherwise:

- macro encoder positions may fail to follow the live-selected property
- `OPT` may keep the wrong quantization/config while holding `LEFT_BOTTOM` or `LEFT_CENTER`

### Recommended Approach

Keep the current sync system, but change its gating logic:

- blocking overlays: still suppress inline resync as today
- inline step-property mode: resync for the current active property
- inline quick-control mode: resync `OPT` for the focused quick control, while macro encoders remain mapped to step property editing unless deliberately overridden

Do not couple inline modes to `overlays.hasVisible()`.

---

## 9. Shared Mapping Utilities

To avoid duplicated logic, extend [SequencerInputUtils.hpp](../src/handler/sequencer/SequencerInputUtils.hpp).

Add shared helpers for:

- quick control item enum
- `CH / DIV / LEN` to normalized mapping
- normalized to `CH / DIV / LEN`
- encoder config for each quick control item
- formatting helpers for compact inline display

This prevents:

- encoding rules living in handlers
- display rules drifting from edit rules
- duplicated discrete mapping tables

---

## 10. Visual Layout Plan

### Top Area

The top sequencer area should become:

1. text header row
2. pattern quick controls row
3. step property strip row
4. step grid

This lets us remove low-value informational clutter from the bottom of the grid.

### Remove or Relocate Bottom Grid Labels

Move these responsibilities out of the bottom corners of the step grid:

- `Track 1`
- `8 steps`

Replace with:

- `CH` in quick controls
- `LEN` in quick controls

Result:

- less visual noise in the grid
- more explicit editability in the top area
- better alignment between visible control and actual interaction

---

## 11. Anti-Patterns to Avoid

1. Do not keep inline held modes inside `OverlayManager`.
2. Do not call the current channel selector `TRACK`.
3. Do not split edit mapping logic between view and handler.
4. Do not create a preview-only highlight state for `LEFT_BOTTOM`.
5. Do not let `OPT` silently switch semantic role without visible focus.
6. Do not add another monolithic block back into `SequencerView`.
7. Do not let quick controls mutate grid-specific UI state directly.

---

## 12. Implementation Plan

### Phase 1: Inline State Foundation

Files:

- [SequencerState.hpp](../src/state/sequencer/SequencerState.hpp)

Tasks:

- add `StepPropertyInlineSelectorState`
- add `PatternQuickControlState`
- keep old overlay state temporarily for migration

Acceptance:

- builds cleanly
- no behavior change yet

### Phase 2: Live Step Property Selection

Files:

- `src/handler/sequencer/SequencerStepPropertyModeHandler.hpp`
- `src/handler/sequencer/SequencerStepPropertyModeHandler.cpp`
- [StepPropertyStrip.cpp](../src/ui/sequencer/StepPropertyStrip.cpp)
- [StandaloneContext.cpp](../src/context/StandaloneContext.cpp)

Tasks:

- replace overlay-based property selection with inline hold mode
- update `activeStepProperty` during `NAV` movement, not only on release
- keep cancel support via snapshot
- remove `SEQ_PROPERTY_SELECTOR` from normal interaction flow

Acceptance:

- holding `LEFT_BOTTOM` changes the actual edited property live
- macros and `OPT` follow immediately
- release keeps the current property
- cancel restores snapshot cleanly

### Phase 3: Add Pattern Quick Controls Component

Files:

- `src/ui/sequencer/PatternQuickControls.hpp`
- `src/ui/sequencer/PatternQuickControls.cpp`
- [SequencerView.hpp](../src/ui/view/SequencerView.hpp)
- [SequencerView.cpp](../src/ui/view/SequencerView.cpp)

Tasks:

- add new top inline control row
- display `CH / DIV / LEN`
- move bottom informational labels out of the grid
- keep rendering dirtiness granular

Acceptance:

- quick controls appear in the sequencer view only
- layout remains readable on target hardware

### Phase 4: Add Pattern Quick Control Handler

Files:

- `src/handler/sequencer/SequencerPatternQuickControlHandler.hpp`
- `src/handler/sequencer/SequencerPatternQuickControlHandler.cpp`
- [SequencerInputUtils.hpp](../src/handler/sequencer/SequencerInputUtils.hpp)
- [StandaloneContext.cpp](../src/context/StandaloneContext.cpp)

Tasks:

- inline hold/select/edit logic for `LEFT_CENTER`
- `NAV` moves between `CH / DIV / LEN`
- `OPT` edits focused item live
- `LEFT_TOP` cancels
- `NAV` press opens advanced overlay if present

Acceptance:

- `LEFT_CENTER` no longer opens a mandatory overlay
- `OPT` edits the focused inline field immediately
- values remain clamped and encoder-configured correctly

### Phase 5: Encoder Sync Cleanup

Files:

- [StandaloneContext.cpp](../src/context/StandaloneContext.cpp)

Tasks:

- distinguish inline modes from true blocking overlays
- ensure macro encoder and `OPT` config resync correctly during holds
- avoid unnecessary position churn while editing

Acceptance:

- no encoder jumps during hold
- no stale quantization when switching inline mode
- no audible regression from UI interaction

### Phase 6: Optional Advanced Overlay Reuse

Files:

- [SequencerPatternConfigHandler.cpp](../src/handler/sequencer/SequencerPatternConfigHandler.cpp)
- any new selector overlays if needed

Tasks:

- reduce old pattern overlay to advanced detail only, or remove it
- keep only what still provides value beyond inline editing

Acceptance:

- overlays become rare and intentional
- no duplicated default workflow remains

---

## 13. Acceptance Criteria

The roadmap is complete when all of the following are true:

- `LEFT_BOTTOM` is live-select and live-edit
- `LEFT_CENTER` is inline quick control selection and editing
- `CH / DIV / LEN` are visible in the top area
- bottom grid informational labels are no longer carrying editable information
- step property selection no longer depends on `OverlayManager`
- encoder sync respects inline modes without becoming unstable
- `SequencerView` remains an assembler, not a new monolith
- all changes build cleanly and remain testable

---

## 14. Recommended Commit Strategy

Implement in small, reviewable commits:

1. `Add inline sequencer selector state scaffolding`
2. `Make step property selection live and overlay-free`
3. `Add inline pattern quick controls component`
4. `Support inline CH DIV LEN editing`
5. `Refine encoder sync for inline sequencer modes`
6. `Retire old sequencer selection overlays`

This keeps regressions localized and makes hardware validation easier.

---

## 15. Conclusion

This roadmap is feasible and aligns with the current architecture, provided one rule is respected:

inline held modes must stop pretending to be overlays.

Once that is done, the remaining work is straightforward:

- dedicated inline state
- dedicated inline handlers
- one new compact UI component
- careful encoder sync cleanup

The resulting sequencer should feel less like a menu-driven UI and more like an instrument, while staying maintainable in code.
