# Sprint 2: Input And Overlay State-Machine Recognition

Updated: 2026-04-29

Purpose: establish the current input/overlay state-machine map before changing
modal behavior.

Current status: recognition complete enough to plan Sprint 2, with native tests
added for the sequencer inline handlers, top-level view switcher, and sequencer
step-edit overlay, sequencer macro-property editing, macro value input, and
transport play toggle. No modal behavior change has been made in this pass.

## Scope

Sprint 2 turns input binding knowledge into semantic interaction knowledge.

Included:

- Map high-risk handlers, scopes, overlays, `.when(...)` predicates, latches,
  and long-press flows.
- Separate observed behavior from intended contracts.
- Identify missing tests before changing behavior.
- Produce a staged plan for targeted state-machine tests and any follow-up
  refactor.

Excluded:

- Full hardware input validation.
- Full LVGL visual validation.
- Rewriting the input framework.
- Broad UI refactors outside the handlers and state-machine surfaces listed
  below.

## Current Source Checks

Run from `midi-studio/core` or anywhere in the workspace:

```powershell
ms test core
rg -n "\.button\(|\.encoder\(|\.when\(|\.scope\(|\.longPress\(" src/handler -g "*.cpp" -g "*.hpp"
rg -n "OverlayType|registerCleanup|show\(|hide\(|current\(|hasVisible|scope\(" src/context src/handler src/state src/ui -g "*.cpp" -g "*.hpp"
```

Current verification:

- `ms test core` passes `44/44` after the first Sprint 3 structure-coverage
  tranche.
- `ms` resolves the workspace directly; do not document `uv run ms ...` for core
  unit-test workflows.

## Binding Inventory

Mechanical count from current `src/handler` files:

| Handler file | Buttons | Encoders | `when` | Scopes | Long press |
|---|---:|---:|---:|---:|---:|
| `src/handler/macro/MacroEditHandler.cpp` | 11 | 5 | 0 | 16 | 1 |
| `src/handler/macro/MacroPerformanceHandler.cpp` | 17 | 5 | 20 | 22 | 3 |
| `src/handler/macro/MacroValueHandler.cpp` | 0 | 1 | 1 | 1 | 0 |
| `src/handler/sequencer/SequencerMacroPropertyHandler.cpp` | 0 | 2 | 2 | 2 | 0 |
| `src/handler/sequencer/SequencerPatternQuickControlsHandler.cpp` | 3 | 2 | 5 | 5 | 0 |
| `src/handler/sequencer/SequencerPropertySelectorHandler.cpp` | 3 | 1 | 4 | 4 | 0 |
| `src/handler/sequencer/SequencerStepEditHandler.cpp` | 4 | 2 | 1 | 6 | 1 |
| `src/handler/sequencer/SequencerStepHandler.cpp` | 13 | 2 | 15 | 15 | 3 |
| `src/handler/settings/DataManagerHandler.cpp` | 8 | 2 | 1 | 10 | 1 |
| `src/handler/settings/GlobalSettingsHandler.cpp` | 5 | 2 | 1 | 6 | 1 |
| `src/handler/transport/TransportHandler.cpp` | 1 | 0 | 1 | 1 | 0 |
| `src/handler/view/ViewSwitcherHandler.cpp` | 3 | 1 | 1 | 4 | 0 |

Interpretation:

- The highest-risk semantic surfaces are `MacroPerformanceHandler` and
  `SequencerStepHandler`: both multiplex NAV, bottom buttons, selection mode,
  add-slot preview, copy/paste, delete/remove, and long-press suppression.
- The next high-risk group is overlay/dialog ownership:
  `DataManagerHandler`, `GlobalSettingsHandler`, `MacroEditHandler`,
  `SequencerStepEditHandler`, and `ViewSwitcherHandler`.
- Sequencer inline modes have fewer bindings, but several compete for the same
  buttons and predicates: quick controls, property selector, macro-property
  editing, and step edit.

## Overlay And Scope Model

Tracked overlay types:

- `PAGE_SELECTOR`
- `MACRO_EDIT`
- `MACRO_EDIT_SELECTOR`
- `MACRO_EDIT_MACRO_SELECTOR`
- `VIEW_SELECTOR`
- `SEQ_STEP_EDIT`
- `GLOBAL_SETTINGS`
- `GLOBAL_SETTINGS_SELECTOR`
- `DATA_MANAGER`
- `DATA_MANAGER_DIALOG`

Observed ownership:

- `StandaloneOverlayAssembly` owns the view selector overlay controller and
  active-view scope provider.
- `MacroFeatureModule` registers macro edit, macro value selector, page selector,
  and macro-target selector cleanup scopes.
- `SequencerFeatureModule` registers `SEQ_STEP_EDIT` and wires sequencer inline
  handlers on the sequencer view scope.
- `SettingsFeatureModule` registers global settings, global settings selector,
  Data Manager, and Data Manager dialog scopes.
- `CoreStateBootstrap` registers overlay state signals with the global overlay
  visibility stack.

## Current Semantic Facts

Confirmed from source:

- View switcher opens on `LEFT_TOP` press only when no overlay is visible, no
  structure selection is active, and sequencer inline modes are inactive.
- Global settings opens on `LEFT_TOP` long press when settings overlays are not
  visible and the current overlay is either `NONE` or `VIEW_SELECTOR`.
- Data Manager opens on `NAV` long press from active top-level view scopes only
  when the current overlay is `NONE`.
- Macro performance and sequencer step structure flows both use NAV long press
  to enter selection mode and suppress the matching release with a
  `*_long_press_used_` flag.
- Macro performance and sequencer step flows both use bottom-left and
  bottom-right short/long press pairs for erase/remove and copy/paste.
- Sequencer quick controls and property selector are inline modes, not normal
  overlay scopes; they block other sequencer open predicates through state flags.
- Step edit is a normal overlay scope and blocks inline open predicates through
  `overlays.hasVisible()`.
- Transport play toggle is bound to `BOTTOM_CENTER` in the active top-level view
  scopes and is guarded by a `.when(...)` predicate for
  `statusBar.transportLocked`.
- Macro value input is bound in macro view scope and is guarded by a
  `.when(...)` predicate for active view, overlay visibility, macro edit
  visibility, and macro quick controls.
- Sequencer macro-property editing is bound in sequencer view scope and is
  blocked by overlays, structure selection, and pattern quick controls.

## Semantic Control Matrix

This matrix records observed and tested ownership for the first Sprint 2
contract tranche. It is intentionally scoped to input surfaces that now have
native tests.

| Control | Normal owner | Modal owner | Tested contract |
|---|---|---|---|
| `LEFT_TOP` press/release | `ViewSwitcherHandler` opens view selector from top-level view scopes | View selector release closes and confirms; inline sequencer modes use release as cancel | `test_ViewSwitcherHandler`, `test_SequencerInlineHandlers`, `test_SequencerStepEditHandler` |
| `LEFT_CENTER` press/release | `SequencerPatternQuickControlsHandler` opens/applies pattern quick controls in sequencer view | None; it is an inline state flag, not an overlay | `test_SequencerInlineHandlers` |
| `LEFT_BOTTOM` press/release | `SequencerPropertySelectorHandler` opens/applies active step-property selector in sequencer view | None; it is an inline state flag, not an overlay | `test_SequencerInlineHandlers` |
| `BOTTOM_CENTER` release | `TransportHandler` toggles play in top-level view scopes | Blocked by overlay authority before it reaches the top-level scope | `test_TransportHandler` |
| `NAV` release | Confirms view selector or sequencer step edit when those overlay scopes own authority | Long press ownership belongs to Data Manager/structure handlers outside this first matrix tranche | `test_ViewSwitcherHandler`, `test_SequencerStepEditHandler` |
| `NAV` encoder | Navigates view selector, inline quick/property selector, or step-edit row depending on current state/scope | Overlay authority wins for overlay-scoped handlers; inline handlers rely on state predicates | `test_ViewSwitcherHandler`, `test_SequencerInlineHandlers`, `test_SequencerStepEditHandler` |
| `OPT` encoder | Edits focused sequencer quick-control value, step-edit row value, or focused sequencer step property | Blocked by overlay authority or explicit sequencer predicates as applicable | `test_SequencerInlineHandlers`, `test_SequencerStepEditHandler`, `test_SequencerMacroPropertyHandler` |
| `MACRO_i` encoder | Edits macro value in macro view or active sequencer step property in sequencer view | Blocked by overlays and competing modal/inline states | `test_MacroValueHandler`, `test_SequencerMacroPropertyHandler` |
| `MACRO_i` long press/release | Opens sequencer step edit for the matching step in the visible page | Opening release is ignored once; later matching release applies | `test_SequencerStepEditHandler` |

## Existing Test Coverage

Good coverage already exists for:

- `test_MacroPerformanceHandler`
- `test_SequencerStepHandler`
- `test_SequencerInlineHandlers`, covering
  `SequencerPatternQuickControlsHandler` and
  `SequencerPropertySelectorHandler`
- `test_ViewSwitcherHandler`
- `test_SequencerStepEditHandler`
- `test_SequencerMacroPropertyHandler`
- `test_MacroValueHandler`
- `test_TransportHandler`
- `test_MacroEditHandler`
- `test_DataManagerHandler`
- `test_GlobalSettingsHandler`
- `test_ModalSelectionUtils`
- related state/domain tests for Data Manager, Global Settings, Macro Edit,
  Macro Performance, and sequencer UI state.

All named Sprint 2 handler surfaces now have first-class native tests or
pre-existing handler coverage. Future tests should be driven by new findings in
the semantic matrix, not added mechanically.

## Sprint 2 Risks

- Encoding accidental priority as a test could freeze bad UX.
- Inline modes and overlay scopes are different mechanisms; treating them as
  identical would hide real conflict cases.
- Long-press flows often need release suppression. Tests must assert both the
  long-press transition and the following release behavior.
- Multiple handlers bind the same physical controls in the same view scope.
  Predicate truth tables need to say which binding should win, not just which
  handlers mention the control.

## Action Plan

Gate 0: regenerate the live map.

- Re-run the binding inventory commands above.
- Produce a table by physical control: `LEFT_TOP`, `LEFT_CENTER`,
  `LEFT_BOTTOM`, `BOTTOM_LEFT`, `BOTTOM_CENTER`, `BOTTOM_RIGHT`, `NAV`,
  `MACRO_i`, `NAV encoder`, `OPT encoder`, `MACRO_ENCODER_i`.
- For each row, list owner, scope, predicate, mode entered, mode exited, and
  release suppression behavior.

Exit signal: a reader can answer "who owns this control in this mode?" from a
tracked table, without grep archaeology.

Gate 1: write intended contracts for the highest-risk modes.

- Macro performance: normal navigation, quick controls, clutch, selection mode,
  add-slot preview, bottom-left remove/delete, bottom-right copy/paste.
- Sequencer structure: normal step edit, page/track navigation, selection mode,
  add-slot preview, bottom-left remove/delete, bottom-right copy/paste.
- View switching: open/close/confirm and blocking predicates.
- Settings/Data Manager: overlay/dialog entry, nested selector/dialog behavior,
  cancel/apply behavior.

Exit signal: each mode has "observed behavior" and "intended contract" recorded
separately.

Gate 2: add missing targeted tests before changing behavior.

Prioritized tests:

1. `test_SequencerInlineHandlers`: covers quick controls open/apply/cancel,
   property selector open/apply/cancel, snapshot restoration, and blocking when
   overlays or competing inline/structure states are active. Done in the first
   Sprint 2 test-only patch.
2. `test_ViewSwitcherHandler`: cannot open while structure selection, quick
   controls, property selector, or another overlay is active; confirms selected
   view on close. Done after decoupling the handler from LVGL object pointers.
3. `test_SequencerStepEditHandler`: long-press opens step edit, release
   suppression, apply/cancel snapshot behavior. Done.
4. `test_SequencerMacroPropertyHandler`: macro encoder and OPT edits, inline
   feedback, and modal blockers. Done.
5. `test_MacroValueHandler`: macro encoder value updates, MIDI CC output, and
   modal blockers. Done.
6. `test_TransportHandler`: play toggle and transport lock. Done.
7. Focused regression tests for any conflict discovered in Gate 0/1.

Exit signal: planned behavior changes have tests that fail before the change or
would fail if the contract regresses.

Gate 3: refactor only after tests expose a real ambiguity.

- Prefer extracting predicate helpers or a tracked state-machine table over a
  broad new abstraction.
- Do not change input semantics and rendering in the same patch.
- Keep handler-local comments minimal; durable contracts belong in `.hpp` or
  this Sprint 2 doc.

Exit signal: no behavior change lands without an explicit contract and a
targeted test.

## Sprint 2 Patches

The first implementation patch is test-only:

- Added `test_SequencerInlineHandlers`.
- Covered the two inline sequencer handlers that are already in the native core
  CMake target.
- Kept production code unchanged.

The second implementation patch keeps behavior unchanged but improves the test
boundary:

- `ViewSwitcherHandler` now consumes an overlay `ScopeID` instead of a
  `lv_obj_t*`/`OverlayBindingContext`.
- `StandaloneGlobalHandlerAssembly` remains the LVGL boundary and translates the
  view-selector element into a scope with `oc::ui::lvgl::scopeID(...)`.
- `src/handler/view/*.cpp` is part of the native core test target.
- Added `test_ViewSwitcherHandler`.

This keeps LVGL in the standalone UI assembly layer while allowing handler
state-machine behavior to be tested with numeric scopes.

The third implementation patch is also behavior-preserving:

- Added `test_SequencerStepEditHandler`.
- Covered long-press entry, ignored opening release, apply, cancel snapshot
  restoration, and blocking predicates for overlays, structure selection, and
  competing inline modes.

The fourth implementation patch closes the named Sprint 2 handler-test tranche:

- Added `test_SequencerMacroPropertyHandler`.
- Added `test_MacroValueHandler`.
- Added `test_TransportHandler`.
- Removed stale tempo-binding claims from `TransportHandler`; current transport
  behavior is play/stop toggle only.

The follow-up API review simplified the handlers against Open Control input
best practices:

- Moved remaining action-level guards in `MacroValueHandler` and
  `TransportHandler` to `.when(...)` predicates.
- Removed unused `SequencerTrackBankState` dependencies from inline sequencer,
  step-edit, and sequencer macro-property handlers.
- Kept `OverlayManager` as the authority boundary instead of duplicating
  overlay checks in callbacks.

Validation:

```powershell
ms test core
git diff --check
```

## Open Questions

- Should global settings be allowed to open while `VIEW_SELECTOR` is active, or
  should all settings overlays require `OverlayType::NONE` like Data Manager?
- Should transport play toggle be globally suppressed by all overlays, or only
  by scopes that naturally stop receiving the top-level binding?
- Should sequencer inline modes be promoted to overlay-like state for conflict
  reporting, or remain lightweight state flags?
