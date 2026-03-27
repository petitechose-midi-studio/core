# Core Alignment Roadmap

> Status: active roadmap
> Scope: `midi-studio/core`
> Goal: align `core` with the best architectural properties seen in `plugin-bitwig` without over-abstracting the domain logic
> Last updated: 2026-03-28

## 1. Outcome Sought

This roadmap aims to make `core`:

- easier to read
- safer to extend
- clearer about ownership and lifecycle
- more stable as a public dependency for downstream projects

The target state is:

- views only project state
- handlers remain explicit and domain-specific
- state becomes more passive
- runtime and timeout logic moves out of UI
- composition root becomes smaller and easier to reason about

## 2. Guardrails

### Allowed abstractions

- navigation helpers such as `stepFromDelta`, `wrapIndex`, clamp helpers
- modal lifecycle helpers when they are purely mechanical
- view-model builders and render-state builders
- runtime services for pulses, timeouts, and transient feedback
- RAII modules for feature wiring and cleanup

### Forbidden abstractions

- generic handlers that replace domain handlers
- utility layers that write across unrelated branches of `CoreState`
- helpers that hide business transitions or overlay semantics
- UI code that writes back into state for convenience
- breaking exported headers used by downstream repos without compatibility

## 3. Tracking Rules

### Status legend

- `todo`: not started
- `doing`: in progress
- `blocked`: waiting on a decision or prerequisite
- `done`: implemented and verified

### Update rule

When a task changes state:

1. update the status marker in this file
2. add the date in the notes line
3. keep the acceptance criteria unchanged unless the scope is explicitly redefined

## 4. Current Baseline

- `core` build: `pio run -e dev` passes
- `plugin-bitwig` build: `pio run -e dev` passes
- public compatibility restored for downstream include of `state/ViewManager.hpp`

## 5. Progress Tracker

## Phase 0. Public API Stability

- `[done]` P0.1 Restore exported `ViewManager.hpp` compatibility
  Files:
  - `src/state/ViewManager.hpp`
  Acceptance:
  - downstream include path remains valid
  - `plugin-bitwig` dev build passes again
  Notes: completed 2026-03-27

## Phase 1. Remove UI-Owned State Mutations

- `[done]` P1.1 Move transport pulse expiry out of `TransportBar`
  Files:
  - `src/ui/transportbar/TransportBar.cpp`
  - `src/state/StatusBarState.hpp`
  - new runtime/service file if needed
  Acceptance:
  - no `state_.*.set(...)` from `TransportBar`
  - pulse reset still behaves identically on device
  Notes: completed 2026-03-27

- `[done]` P1.2 Move sequencer inline feedback timeout out of `SequencerView`
  Files:
  - `src/ui/view/SequencerView.cpp`
  - `src/state/sequencer/SequencerUiState.hpp`
  - runtime/service file if needed
  Acceptance:
  - `SequencerView` no longer advances feedback state on its render timer
  - timeout logic remains deterministic and testable
  Notes: verified on 2026-03-27 in current snapshot; timeout is owned by `SequencerState::updateUi()`

- `[done]` P1.3 Add a guard test for "UI does not own state transitions"
  Files:
  - new tests under `test/`
  Acceptance:
  - transient feedback lifecycle is covered by tests
  - regressions are detectable without LVGL rendering
  Notes: completed 2026-03-27 with native lifecycle tests covering inline feedback and status-bar pulse expiry

## Phase 2. Shrink the Composition Root

- `[done]` P2.1 Extract `StandaloneContext` creation steps into named methods
  Files:
  - `src/context/StandaloneContext.cpp`
  - `src/context/StandaloneContext.hpp`
  Acceptance:
  - init path is split by concern
  - cleanup path mirrors init structure
  - behavior remains unchanged
  Notes: completed 2026-03-27; `init()` and `onCleanup()` now delegate to named creation/cleanup phases

- `[done]` P2.2 Introduce feature wiring modules
  Files:
  - new files under `src/context/standalone/` or a dedicated feature folder
  Acceptance:
  - macro, sequencer, and settings wiring are no longer all assembled inline in one file
  - module cleanup is RAII-based
  Notes: completed 2026-03-27 with `MacroFeatureModule`, `SequencerFeatureModule`, and `SettingsFeatureModule`

- `[done]` P2.3 Add lifecycle tests for overlay cleanup and view switching
  Files:
  - new tests under `test/`
  Acceptance:
  - overlay hide/cleanup is covered
  - view activation/deactivation ordering is covered
  Notes: completed 2026-03-27; overlay cleanup remains covered by `test_CoreStateAuthority`, and active-view ordering is covered by `test_ActiveViewLifecycle`

## Phase 3. Make `CoreState` More Passive

- `[done]` P3.1 Reduce `CoreState` facade surface
  Files:
  - `src/state/CoreState.hpp`
  - related workflow files
  Acceptance:
  - `CoreState` is primarily data plus simple invariants
  - orchestration moves to workflows/services
  Notes: completed 2026-03-28; removed workflow-forwarding facade methods for macro/data-manager/sequencer persistence, and moved remaining macro runtime/config helpers to `MacroWorkflow`

- `[done]` P3.2 Revisit `friend` usage and tighten ownership boundaries
  Files:
  - `src/state/CoreState.hpp`
  - `src/state/CoreStateBootstrap.*`
  - `src/state/CoreStateLifecycle.*`
  - `src/state/DataManagerWorkflow.*`
  Acceptance:
  - only necessary collaborators keep privileged access
  - responsibilities are easier to infer from the type surface
  Notes: completed 2026-03-28; removed `friend` access for macro/data-manager/sequencer workflows, leaving only `CoreStateBootstrap` and `CoreStateLifecycle`

- `[done]` P3.3 Harmonize persistence error contracts
  Files:
  - `src/state/CoreSettings.*`
  - `src/persistence/PersistenceSlotFileStore.hpp`
  - `src/persistence/SequencerPersistence.hpp`
  Acceptance:
  - persistence APIs expose failures consistently
  - silent write/commit failures are reduced or made explicit
  Notes: completed 2026-03-28 with shared `PersistenceWriteStatus`, explicit status-returning save/commit APIs, and targeted failure tests

## Phase 4. Factorize Mechanics, Not Domain Logic

- `[done]` P4.1 Extract shared navigation helpers
  Files:
  - new helper under `src/handler/` or `src/util/`
  Acceptance:
  - repeated `delta -> step -> wrap` code is reduced
  - domain handlers stay explicit and separate
  Notes: completed 2026-03-28; `NavigationUtils.hpp` is the shared navigation surface and is now used across settings, macro, and sequencer handlers

- `[done]` P4.2 Extract shared modal mechanics only where behavior is identical
  Files:
  - candidate handlers:
    - `src/handler/settings/GlobalSettingsHandler.cpp`
    - `src/handler/settings/DataManagerHandler.cpp`
    - `src/handler/macro/MacroEditHandler.cpp`
    - `src/handler/sequencer/SequencerPatternConfigHandler.cpp`
  Acceptance:
  - no generic "do everything" modal handler exists
  - business transitions remain visible in each domain handler
  Notes: completed 2026-03-28 with minimal `ModalSelectionUtils.hpp` helpers for wrapped selection and hide/reset mechanics; domain handlers remain separate and explicit

- `[done]` P4.3 Add a review rule for prohibited abstraction drift
  Files:
  - documentation only
  Acceptance:
  - future generic-handler shortcuts are easier to reject during review
  Notes: completed 2026-03-28 with `docs/ARCHITECTURE_REVIEW_RULES.md`

## Phase 5. Finish the UI Decomposition

- `[done]` P5.1 Continue extracting `StepGrid` logic into pure helpers
  Files:
  - `src/ui/sequencer/StepGrid.cpp`
  - `src/ui/sequencer/StepGridFrameLogic.*`
  - `src/ui/sequencer/StepGridLabelLogic.*`
  - `src/ui/sequencer/StepGridRenderLogic.*`
  Acceptance:
  - `StepGrid` becomes a thin LVGL renderer
  - geometry, diffing, and frame computation are easier to test separately
  Notes: completed 2026-03-28 with geometry/layout extraction into `StepGridGeometryLogic.*`; frame, diff, label, render-style, and geometry calculations now live outside `StepGrid.cpp`

- `[done]` P5.2 Extend view-model builders where UI still mixes state reads and rendering
  Files:
  - `src/ui/view/MacroViewModelBuilder.*`
  - `src/ui/sequencer/SequencerViewModelBuilder.*`
  - related views
  Acceptance:
  - views mostly consume prepared props/frame state
  Notes: completed 2026-03-28; `MacroView` now renders from `MacroViewFrameState`, and `SequencerView` already consumed dedicated builder outputs for header, quick-controls, strip, and grid

## Phase 6. Downstream Safety Net

- `[done]` P6.1 Add a lightweight downstream compatibility check
  Files:
  - docs and/or build scripts
  Acceptance:
  - changes to public headers are caught earlier
  - `core` updates are less likely to break `plugin-bitwig`
  Notes: completed 2026-03-27 with `script/dev/check-downstream-compat.ps1` and docs entry in `docs/README.md`

- `[done]` P6.2 Keep this roadmap updated as the single progress tracker
  Files:
  - `docs/CORE_ALIGNMENT_ROADMAP.md`
  Acceptance:
  - roadmap reflects real status
  - completed work is marked with dates
  Notes: completed 2026-03-28; roadmap now reflects all implemented phases and completion dates

## 6. Recommended Execution Order

1. Phase 1
2. Phase 2
3. Phase 6.1
4. Phase 3
5. Phase 4
6. Phase 5

## 7. Done Criteria For The Whole Roadmap

The roadmap can be considered complete when:

- UI no longer owns transient state transitions
- `StandaloneContext` is no longer a high-risk central hotspot
- `CoreState` is mostly data and simple invariants
- modal handlers remain explicit while sharing only safe mechanics
- `StepGrid` is decomposed enough to reason about locally
- downstream compatibility is treated as a first-class requirement
