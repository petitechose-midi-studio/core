# Sequencer Codebase Cleanup Audit

> Status: working document  
> Scope: `midi-studio/core` with emphasis on the sequencer path and its coupling to app/runtime infrastructure

## Goal

Stabilize the codebase before adding more sequencer features.

The objective is not to "rewrite" the app. The objective is to:

- reduce cross-module coupling
- isolate UI from interaction logic more consistently
- shrink the number of places that understand sequencer editing rules
- retire obsolete overlay paths that now conflict with the inline workflow
- make the next features land in clean extension points

## Method

This audit is based on the current code structure, file sizes, ownership boundaries, and the recent sequencer refactors already in place.

Hotspots identified from the current tree:

- `src/context/StandaloneContext.cpp` ~1405 lines
- `src/ui/sequencer/StepGrid.cpp` ~1063 lines
- `src/state/CoreState.hpp` ~850 lines
- `src/state/sequencer/SequencerState.hpp` ~413 lines
- `src/handler/sequencer/SequencerInputUtils.hpp` ~290 lines
- `src/ui/sequencer/SequencerHeaderBar.cpp` ~290 lines
- `src/ui/view/SequencerView.cpp` ~272 lines

## What Is Already In Better Shape

The recent refactors already moved the codebase in the right direction:

- `SequencerView` is no longer the old monolith
- `StepPropertyStrip` is isolated
- `PatternQuickControls` exists as a separate component
- `StepGrid` is isolated from the view assembly layer
- sequencer runtime is no longer owned by the UI context
- step-property selection is now an inline mode instead of a modal overlay

This means the codebase does **not** need a redesign from scratch. It needs a cleanup pass around the current structure.

## Main Findings

### 1. `StandaloneContext` Still Owns Too Much

`StandaloneContext` is still acting as:

- application bootstrapper
- view assembler
- overlay registry
- overlay renderer
- input binding registry
- encoder synchronization coordinator
- state/render bridge

This is the largest structural issue in the core app.

The file is not large just because of "setup code". It also owns rules that should live closer to the domain they affect.

Current smell:

- sequencer overlay rendering and macro overlay rendering live in the same file
- encoder sync policy for the sequencer lives here
- view and overlay lifecycles are strongly coupled here
- recent inline modes forced more `if overlay visible / if selecting` branching into the context

### 2. `SequencerState` Mixes Engine, UI Mode, and Ephemeral Interaction State

`SequencerState` currently extends engine state and also stores:

- visible page and focused step
- active property
- inline selector state
- inline feedback state
- overlay state
- pattern quick-control state

This is still workable, but the file has started to accumulate responsibilities from multiple layers:

- engine-facing pattern data
- sequencer view model
- transient UI interaction state

This is a maintainability risk because every new interaction feature naturally ends up in the same struct.

### 3. `StepGrid` Still Carries Too Many Responsibilities

`StepGrid.cpp` is much better than the old `SequencerView.cpp`, but it is still a hotspot because it mixes:

- LVGL object creation
- static geometry caching
- per-step render cache diffing
- note shape rendering
- playback indicator rendering
- label formatting and inline feedback logic
- probability masking visuals
- property-specific visual rules

This is now the main UI hotspot.

The risk is not just file size. The risk is that every small visual change still requires touching a file that also owns caching and geometry behavior.

### 4. Inline Workflow and Legacy Overlay Workflow Still Coexist

The codebase currently contains both:

- new inline `LEFT_BOTTOM` / `LEFT_CENTER` modes
- legacy overlay paths such as sequencer pattern config overlay

That coexistence is acceptable temporarily, but not as a stable architecture.

If left in place too long, the app will keep two competing interaction models:

- frequent inline editing
- old modal editing

This increases branching in handlers, encoder sync logic, and rendering setup.

### 5. Sequencer Input Logic Is Correct but Fragmented

Sequencer interaction rules are currently distributed across:

- `SequencerPropertySelectorHandler`
- `SequencerPatternQuickControlsHandler`
- `SequencerMacroPropertyHandler`
- `SequencerStepHandler`
- `SequencerStepEditHandler`
- `SequencerInputUtils.hpp`

This is not yet broken, but the rules are spread far enough that future changes will increasingly require touching multiple files to preserve consistency.

Typical example:

- change a property-editing rule
- then update encoder config
- then update macro/OPT behavior
- then update focus/page clamping

That coupling exists today, just spread across handlers and helpers.

### 6. Header / Quick Control Composition Is Mid-Transition

The top area is improving, but there is still transitional code:

- `SequencerHeaderBar` mostly renders the progress strip
- `PatternQuickControls` now owns the semantic top-row information
- some older text semantics still exist in the header props path

This is not critical, but the ownership boundary is not yet final.

### 7. Large Core Aggregates Will Keep Growing Without Sub-State Separation

`CoreState.hpp` and `SequencerState.hpp` are not a problem by themselves, but they are clear warning signals.

The codebase needs one more split between:

- durable musical state
- UI state
- transient interaction state

If not, future features such as copy/paste, ratchet, or advanced step conditions will make the state layer harder to reason about.

## Cleanup Priorities

### Priority 1: Reduce `StandaloneContext`

This is the highest-value cleanup target.

Target outcome:

- `StandaloneContext` becomes a composition root, not a giant behavior host
- overlay rendering setup is grouped by concern
- sequencer-specific sync logic moves closer to sequencer modules

Recommended extraction path:

1. Extract sequencer overlay presenters / render setup into a dedicated module
2. Extract sequencer encoder sync policy into a dedicated coordinator
3. Extract macro overlay setup similarly
4. Leave `StandaloneContext` with lifecycle, assembly, and high-level wiring only

Suggested modules:

- `src/context/standalone/StandaloneSequencerOverlayBindings.*`
- `src/context/standalone/StandaloneMacroOverlayBindings.*`
- `src/context/standalone/SequencerEncoderSyncCoordinator.*`

### Priority 2: Separate Sequencer UI State from Pattern Data State

Do not split aggressively into ten tiny files. Just create one clean boundary.

Recommended target:

- engine/pattern state stays in `SequencerState` base path
- UI and interaction state move into a dedicated nested struct or separate header

Suggested direction:

- `src/state/sequencer/SequencerUiState.hpp`

This file would own:

- page
- focused step
- active property
- inline selector
- inline feedback
- pattern quick controls
- step edit overlay state
- pattern config overlay state

That keeps the main sequencer state more legible and prevents the engine-facing part from becoming a dumping ground for UI concerns.

### Priority 3: Split `StepGrid` by Responsibility, Not by Widget Count

Do not split `StepGrid` into dozens of microscopic classes.

The useful split is:

- object creation / LVGL tree
- geometry/cache state
- pure render calculations
- tile render application

Suggested extraction path:

- keep `StepGrid` as the owner of LVGL objects
- extract pure tile calculations into one helper module
- extract cache/diff structs into one header

Suggested modules:

- `src/ui/sequencer/StepGridRenderTypes.hpp`
- `src/ui/sequencer/StepGridRenderLogic.hpp/.cpp`

Possible contents:

- `TileRenderState`
- `TileRenderDiff`
- note/marker/guide visual calculations
- visibility rules for enabled/in-pattern/probability-masked states

That will make UI tweaks safer because they will stop touching LVGL creation and render diffing at the same time.

### Priority 4: Retire the Legacy Pattern Config Overlay

The inline quick-control workflow is now the primary path for pattern editing.

That means the old pattern-config overlay path should be removed unless a genuinely advanced sequencer action still needs its own dedicated dialog.

Recommended rule:

- inline mode is default for `Track / Division / Length`
- overlay remains only for advanced actions not worth keeping inline

### Priority 5: Consolidate Sequencer Editing Rules

The current split across handlers is acceptable, but the shared editing logic should stop leaking across multiple files.

The clean next step is not another generic utility header.

The clean next step is a focused interaction module that centralizes:

- which control edits what
- encoder config for each editing mode
- focus clamping rules after length changes
- property and quick-control normalization rules

Suggested direction:

- keep `SequencerInputUtils.hpp` only for pure value conversions
- move orchestration rules into a dedicated coordinator module

Possible module:

- `src/handler/sequencer/SequencerInteractionRules.hpp`

## Proposed Refactor Order

### Phase 1

Stabilize architecture without changing behavior.

- extract sequencer-specific overlay/render setup out of `StandaloneContext`
- extract sequencer encoder sync policy out of `StandaloneContext`
- keep the public behavior identical

### Phase 2

Separate sequencer UI state from engine/pattern state.

- create `SequencerUiState`
- migrate inline selector / feedback / overlay state there
- keep access paths explicit and mechanical

### Phase 3

Reduce `StepGrid` into a more stable rendering owner.

- move pure render structs and calculations out
- keep LVGL ownership local
- make visibility rules easier to read and test

### Phase 4

Retire transitional overlay paths.

- remove the old pattern-config overlay path once inline mode is confirmed
- remove dead branches and compatibility code once inline mode is confirmed

### Phase 5

Document invariants and add lightweight regression tests where possible.

At that point, document:

- inline mode ownership rules
- overlay vs inline responsibilities
- encoder sync invariants
- sequencer render invariants

## Explicit Non-Goals

This cleanup should **not** do the following:

- rewrite all handlers into a new framework
- split every file just to reduce line counts
- move logic into views that belongs in interaction layers
- duplicate state just to make rendering easier
- turn the sequencer into an over-abstracted widget hierarchy

The goal is not abstraction for abstraction's sake. The goal is stable ownership boundaries.

## Recommended Next Concrete Step

The next refactor should be:

1. extract sequencer-specific overlay/render wiring out of `StandaloneContext`
2. extract sequencer encoder sync logic out of `StandaloneContext`
3. leave the rest untouched until that lands cleanly

That is the best leverage point because it reduces the biggest hotspot without forcing a risky UI rewrite.
