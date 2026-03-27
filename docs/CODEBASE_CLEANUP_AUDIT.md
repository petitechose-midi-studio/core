# Codebase Cleanup Audit

> Status: first-pass reconnaissance  
> Scope: `midi-studio/core` overall architecture, with extra focus on sequencer and interaction flow

## Intent

This audit is intentionally critical.

The goal is not to add abstractions for the sake of abstraction. The goal is to identify:

- where responsibilities are still mixed
- where logic is duplicated or drifting
- where the current structure will resist future features
- where cleanup gives real leverage without destabilizing the app

## Executive Summary

The codebase is not structurally broken. It already has good instincts:

- handlers own input
- state is signal-based
- views are mostly projections
- runtime sequencing was moved out of UI ownership
- sequencer UI is already more modular than before

But the project still has three architectural pressure points:

1. **Contexts are too heavy**
2. **State aggregates are too broad**
3. **Some UI components still own orchestration logic rather than pure presentation**

The biggest risk is not "bad code". The biggest risk is **continued feature growth on top of transitional structure**.

## High-Level Findings

### 1. `StandaloneContext` Is Still a Composition Root Plus Too Much Else

Current role mix:

- context lifecycle
- view construction
- overlay creation and registration
- overlay render wiring
- input handler construction
- cross-cutting encoder synchronization
- view switching

This is the clearest hotspot in the app.

This file still behaves like an application shell plus an integration controller plus a presenter host.

That is too much for one unit.

### 2. The Codebase Uses Good Concepts, But They Are Not Yet Grouped by Concern

The project has the right building blocks:

- state
- handlers
- views
- overlays
- runtime services

The problem is that several modules still combine these roles in practice.

Typical examples:

- overlay rendering functions live in the context instead of per-domain presenter modules
- encoder sync policy lived in `StandaloneContext` before the recent extraction
- quick control editing rules are split across handlers and generic helpers

The architecture is directionally correct, but not yet fully normalized.

### 3. State Is Becoming a Catch-All

`CoreState.hpp` and `SequencerState.hpp` are both signs of useful centralization and growing debt.

They currently mix:

- durable domain state
- UI state
- transient interaction state
- overlay state
- persistence-facing state

That is manageable now, but it scales poorly.

The danger is that every new feature has an easy path:

- add a signal
- add a nested struct
- add another watcher

That keeps velocity high short-term, but eventually obscures the ownership of the state itself.

### 4. UI Components Are Cleaner Than Before, But Not Yet “Almost Logic-Free”

If the target is “components should have almost no logic”, then the current codebase is only partway there.

Examples:

- `PatternQuickControls` is still mostly presentational, which is good
- `StepPropertyStrip` is also close to presentation-only
- `MacroView` still owns a timer-driven dirty/update system
- `SequencerView` still owns render invalidation orchestration
- `StepGrid` still owns both LVGL tree management and substantial rendering rules

This is not necessarily wrong, but it is not yet the “thin components” architecture you want.

The real boundary should be:

- components own **LVGL objects and final rendering**
- coordinators/presenters own **what gets rendered and when**
- handlers own **input semantics**

The codebase is not fully there yet.

### 5. Sequencer Architecture Is Better Than Macro Architecture Right Now

This is an important finding.

The sequencer path has already undergone meaningful cleanup:

- runtime separated from UI ownership
- property strip extracted
- quick controls extracted
- step grid extracted

Meanwhile the macro side is still simpler, but also structurally older:

- `MacroView.cpp` still owns update coalescing via an LVGL timer
- top bar rendering is still tightly local
- macro edit overlay flow remains highly context-driven

So the codebase is now **architecturally uneven**:

- sequencer path = newer, more modular
- macro path = simpler but older in structure

This matters because future cleanup should avoid making only the sequencer clean while leaving the app inconsistent elsewhere.

### 6. Overlay Architecture Is Mid-Transition

The project now has two models:

- inline interaction for frequent actions
- overlay interaction for modal detail editing

This is good in principle.

But the boundary is not fully settled:

- some old overlay paths still exist even though inline mode is becoming canonical
- overlay rendering still lives in the context
- input/authority rules are still split between scopes, overlay stack state, and ad hoc guards

The project needs a clearer rule:

- inline for frequent, held, instrument-style interaction
- overlay for advanced or low-frequency modal interaction

Then the remaining transitional paths should be simplified or removed.

## File-Level Critical View

### `src/context/StandaloneContext.cpp`

Status: biggest structural hotspot.

Problems:

- too many responsibilities
- domain-specific render wiring mixed together
- macro and sequencer concerns mixed
- changes here often imply broad regressions

Action:

- keep reducing it into a pure composition root
- extract domain-specific render bindings and coordinators

### `src/ui/sequencer/StepGrid.cpp`

Status: biggest UI hotspot.

Problems:

- owns creation, geometry, cache diffing, and presentation rules
- visual tweaks still require touching low-level render flow
- probability masking and per-property behavior still meet in one file

Action:

- keep LVGL ownership here
- move render-state calculation and diff logic out

### `src/state/CoreState.hpp`

Status: useful aggregate, but too broad.

Problems:

- app-wide state, persistence orchestration, overlay registration and convenience behavior all meet here

Action:

- preserve as top-level aggregate
- move sub-state definitions and per-domain helper logic out

### `src/state/sequencer/SequencerState.hpp`

Status: overloaded.

Problems:

- engine/pattern state mixed with UI and ephemeral interaction state

Action:

- split out `SequencerUiState`
- keep `SequencerState` as the domain aggregate that owns both, but stop defining everything inline

### `src/handler/sequencer/SequencerInputUtils.hpp`

Status: helpful, but at risk of becoming a grab-bag.

Problems:

- pure conversion helpers and workflow-level behavior are too close together

Action:

- keep only value mapping and normalization helpers here
- move workflow rules into a dedicated interaction rules module if needed

### `src/ui/view/MacroView.cpp`

Status: acceptable today, but not aligned with the target architecture.

Problems:

- owns timer-based dirty orchestration
- still mixes presentation with a small render scheduler

Action:

- not urgent
- but eventually the same “thin view” treatment used on the sequencer path should apply here too

## Duplication / Drift Risks

The current codebase does not have catastrophic duplication, but it has **drift-prone duplication**:

- value formatting rules exist across step display and overlay rendering paths
- editing rules live partly in handlers and partly in helper functions
- overlay vs inline flows sometimes solve the same domain concern differently
- top-bar semantics and quick-control semantics are still converging

This means the next risk is not copy-paste duplication. The next risk is **behavioral drift**.

## Architecture Principles Recommended Going Forward

### 1. Components Should Be Presentation Owners, Not Workflow Owners

A component may own:

- LVGL object creation
- style application
- layout
- final render from props

A component should avoid owning:

- state transitions
- editing rules
- cross-component coordination
- input semantics

### 2. Contexts Should Assemble, Not Orchestrate Domains

A context should:

- construct things
- wire them together
- own lifecycle boundaries

A context should avoid:

- formatting domain-specific overlay content
- owning encoder sync policies
- accumulating domain-specific branching

### 3. Keep “Pure Rules” Away From LVGL

Whenever possible, rules such as:

- step visibility
- probability masking state
- label mode
- highlight conditions
- value normalization

should be representable without touching LVGL.

That is the cleanest path to maintainability without overengineering.

### 4. Prefer a Few Strong Modules Over Many Tiny Abstractions

The codebase does **not** need:

- a class per tiny visual detail
- a service per signal watcher
- a factory layer for everything

It **does** need:

- one good sequencer UI state boundary
- one good sequencer interaction/sync boundary
- one good sequencer render-logic boundary
- one cleaner context assembly structure

## Recommended Refactor Order

### Phase 1: Finish Reducing `StandaloneContext`

Continue the work already started.

Next targets:

- sequencer overlay rendering extraction
- macro overlay rendering extraction
- domain-specific binding setup grouped by module

### Phase 2: Split Sequencer Domain State From UI State

Add a dedicated `SequencerUiState` module.

Move:

- page
- focused step
- active property
- inline selector state
- inline feedback state
- quick controls state
- overlay UI state

out of the main state definition body.

### Phase 3: Normalize `StepGrid`

Keep it as the LVGL owner, but reduce its responsibilities.

Extract:

- tile render structs
- diff rules
- step visibility/highlight rules
- pure visual state calculations

### Phase 4: Decide the Final Overlay Surface

Make an explicit call for each remaining sequencer overlay:

- keep as advanced mode
- migrate to inline
- remove

Do not let the transitional state linger.

### Phase 5: Bring Macro Path Up to the Same Standard

Once sequencer architecture is normalized, mirror the same cleanup mindset on:

- `MacroView`
- macro overlay render wiring
- top bar ownership

## Recommended Immediate Next Step

The best next step remains:

1. continue extracting sequencer-specific render/binding wiring out of `StandaloneContext`
2. then split sequencer UI state from sequencer domain state

That order gives the best cleanup leverage without forcing speculative abstractions.
