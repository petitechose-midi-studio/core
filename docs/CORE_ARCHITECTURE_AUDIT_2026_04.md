# Core Architecture Audit and Action Plan

> Date: 2026-04-15
> Scope: `midi-studio/core`
> Status: Working audit grounded in the current codebase
> Purpose: establish a concrete, reproducible architecture baseline before resuming feature work

---

## 1. Objective

This document formalizes the current diagnosis of the `core` codebase and turns it into an actionable cleanup and architecture plan.

Target state:

- no dead or inherited code kept "just in case"
- responsibilities split clearly and predictably
- naming and module boundaries follow one reproducible logic
- standalone runtime wiring is explicit and testable
- a new developer can identify where state lives, where behavior lives, and where UI lives without guesswork

This audit intentionally validates claims against the current repository structure and implementation, not only against architectural intent or historical docs.

### 1.1 This document is the active execution reference

This file is not only an audit. It is the active reference for:

- current diagnosis
- target architecture rules
- tracked cleanup work
- validation requirements
- restart-safe handoff

If work resumes after context loss, thread reset, or a new developer handoff, this document must be sufficient to answer:

- what the real architectural problems are
- what the target state is
- what the active work items are
- how completion is measured
- where to continue next

### 1.2 Status vocabulary

All tracked work items in this document must use one of these statuses:

- `TODO`: not started
- `IN_PROGRESS`: currently active
- `BLOCKED`: cannot continue without a decision or prerequisite
- `DONE`: implemented and validated against stated success criteria
- `CANCELLED`: intentionally dropped and replaced or made irrelevant

Rule:

- at most one major architecture item should be `IN_PROGRESS` per workstream unless parallelism is deliberate and non-overlapping

### 1.3 Mandatory tracking fields for every work item

Each architecture work item must define:

- `ID`
- `Title`
- `Problem`
- `Scope`
- `Deliverables`
- `Validation`
- `Success Criteria`
- `Dependencies`
- `Status`
- `Last Updated`
- `Next Step`

If one of these fields is missing, the item is not execution-ready.

### 1.4 Mandatory closure evidence

No architecture item may move to `DONE` without leaving explicit evidence in the repository or commit history.

Required evidence:

- code changes or explicit "no code change required" rationale
- documentation updates if names, placement, or contracts changed
- tests added or updated when behavior/contracts changed
- validation results recorded in the item notes
- residual risks listed if full closure is intentionally deferred

### 1.5 Resume protocol after interruption

When work resumes after a reset or handoff:

1. read sections 1, 8, 9, and 10 of this document first
2. identify all items not marked `DONE` or `CANCELLED`
3. resume the highest-priority item whose dependencies are already `DONE`
4. update that item's `Status`, `Last Updated`, and `Next Step` before making further code changes
5. at pause or handoff, record:
   - current status
   - code touched
   - tests run
   - blockers
   - exact next action

If this protocol is not followed, the work is not considered safely handoff-ready.

---

## 2. Codebase Snapshot

Validated source layout:

- `src/config`: platform and runtime constants
- `src/context`: composition roots and standalone assembly
- `src/handler`: user interaction workflows
- `src/persistence`: slot storage and persistence codecs
- `src/sequencer`: clock and playback runtime
- `src/state`: authoritative reactive state and lifecycle
- `src/ui`: views and LVGL-facing rendering

Validated tests:

- state and persistence coverage exist
- handler-level coverage exists
- sequencer runtime sync coverage exists
- standalone composition/runtime integration coverage is still weaker than domain coverage

Representative current file sizes:

- `src/handler/sequencer/SequencerStepHandler.cpp`: 169 lines
- `src/handler/sequencer/SequencerStructureNavigationWorkflow.cpp`: 287 lines
- `src/handler/sequencer/SequencerStructureEditWorkflow.cpp`: 299 lines
- `src/handler/macro/MacroPerformanceHandler.cpp`: 241 lines
- `src/handler/macro/MacroStructureWorkflow.cpp`: 340 lines
- `src/handler/macro/MacroPerformanceModeWorkflow.cpp`: 276 lines
- `src/ui/sequencer/SequencerHeaderBar.cpp`: 361 lines
- `src/ui/sequencer/SequencerHeaderBarRenderModel.cpp`: 203 lines
- `src/sequencer/SequencerRuntimeService.cpp`: 456 lines
- `src/state/CoreState.cpp`: 350 lines
- `src/context/StandaloneContext.cpp`: 274 lines

Interpretation:

- repo-level structure is healthy
- local concentration of complexity is now the main maintainability problem

---

## 3. Validated Architectural Strengths

### 3.1 Clear intended direction

The repo consistently states and mostly follows these rules:

- handlers mutate state
- views render state
- widgets should not own workflow logic
- persistence should stay out of UI code

Validated in:

- `README.md`
- `docs/README.md`
- `docs/INVARIANTS.md`
- `docs/STATE_MANAGEMENT.md`

### 3.2 State-first design is real, not decorative

`src/state/CoreState.hpp` is the real authority root for:

- macro domain
- sequencer domain
- shared UI/system domain
- persistence coordination

This is materially better than fragmented view-owned state.

### 3.3 Runtime work is serious and increasingly isolated

`src/sequencer/SequencerRuntimeService.cpp` and `src/sequencer/MidiClockSyncService.cpp` show deliberate handling of:

- internal vs external clock ownership
- transport following
- runtime snapshots
- playback/UI projection split
- MIDI event subscription boundaries

This is a strong base for an instrument-like sequencer.

### 3.4 Persistence design is structured

Persistence is not ad hoc:

- `MacroPersistence` and `SequencerPersistence` use slot/journal-based flows
- settings are separated from workspace/library payloads
- payload versioning and magic values are explicit

Validated in:

- `src/persistence/MacroPersistence.hpp`
- `src/persistence/SequencerPersistence.hpp`
- `src/state/CoreSettings*`

### 3.5 Test culture exists

The test tree shows real effort on:

- core settings
- persistence
- handler behavior
- MIDI clock sync
- sequencer UI state

This is a major positive signal. The codebase is not relying only on manual validation.

---

## 4. Validated Architectural Weaknesses

### 4.1 Standalone composition is still too implicit

This is the most important finding.

Recent real bug:

- `SequencerRuntimeService` existed
- standalone controls toggled play correctly
- playhead did not move in native standalone
- root cause: runtime was not actually being updated in the standalone composition path

Validated in the current wiring:

- `src/context/StandaloneContext.cpp`
- `src/context/standalone/StandaloneFeatureAssembly.cpp`
- `src/context/standalone/SequencerFeatureModule.cpp`

The recent fix solved the symptom by explicitly routing `feature_assembly_->updateRuntimeFeatures()` into `StandaloneContext::update()` and giving `SequencerFeatureModule` ownership of `SequencerRuntimeService`.

Conclusion:

- runtime ownership/update responsibilities were not explicit enough in the architecture
- the composition root allowed a real subsystem to exist without being live

This is not a small bug. It reveals a missing contract.

### 4.2 Complexity is too concentrated in a few central files

This is now the main maintainability risk.

Examples:

- before CA-005, `SequencerStepHandler.cpp` mixed binding registration, navigation rules, structure preview, selection flow, step toggling, and shared-track application logic
- after CA-005, sequencer interaction logic is clearer, but the extracted structure workflows are still substantial and must not regrow into implicit "second handlers"
- before CA-006, `MacroPerformanceHandler.cpp` mixed clutch, quick controls, encoder configuration, structure navigation, selection, and destructive/copy-paste flows
- after CA-006, macro interaction logic is clearer, but the structure and performance-mode workflows are still large enough to require discipline
- before CA-008, `SequencerHeaderBar.cpp` mixed LVGL rendering, strip geometry, cursor layout, and strip visual projection in one file
- after CA-008, header rendering is clearer, but other sequencer UI files still carry non-trivial geometry/model preparation inline
- `SequencerRuntimeService.cpp` is doing orchestration, event subscription, clock ownership, runtime snapshotting and UI publication

These files are still readable with effort, but they are no longer cheap to reason about.

Consequence:

- regressions become easier to introduce during refactors
- onboarding cost rises sharply
- architectural intent becomes harder to see from local code

### 4.3 `CoreState` is coherent but approaching "god object" territory

`CoreState` is currently the best authority root available, but it exposes a very wide mutable surface:

- macro state
- sequencer state
- overlays
- navigation
- clipboard
- settings
- transport/sync status

This improves integration speed but weakens local boundaries.

Consequence:

- handlers can reach too much
- feature modules can depend on too much
- refactors become coupling-sensitive

Current state after CA-007:

- sequencer structure workflows no longer depend directly on `CoreState`; they consume a narrow `SharedTrackDomainServices` façade
- `SequencerRuntimeService` no longer takes `CoreState`; it now declares explicit `StateRefs` for the sequencer runtime slice it actually owns
- the remaining risk is now concentrated more in true cross-domain services and composition roots than in day-to-day interaction handlers

### 4.4 Documentation is directionally useful but partially stale

Validated mismatch examples before CA-001 / CA-002:

- `docs/STANDALONE_INTERACTION_GRAMMAR_SPEC.md` used outdated selector vocabulary and stale file references, while the current structure had moved to shared `StructureNavigationFocus`, `TrackNavigationState.selection`, and per-view page-selection state
- `docs/STATE_MANAGEMENT.md` explicitly warns that some examples are historical/conceptual and that `src/state/CoreState.hpp` is the source of truth

Current state after CA-001 / CA-002:

- stale handler/file references were removed from active contract docs
- tutorial guides now declare themselves non-normative and point to current contract references
- `docs/README.md` now separates normative architecture references from illustrative guides

Conclusion:

- the drift pattern was real and needed explicit cleanup
- the repo is now better labeled for onboarding, but this split must be preserved in future changes

### 4.5 Integration tests lag behind assembly complexity

Domain and handler testing is stronger than composition testing.

Evidence:

- we found a real standalone runtime wiring bug despite decent subsystem coverage
- current tests heavily validate handler/state logic, less so "is the actual standalone path alive and correctly assembled"

Conclusion:

- the codebase needs a small number of higher-value standalone integration tests
- not more unit tests everywhere, but better protection at the composition seam

---

## 5. Naming and Responsibility Diagnosis

### 5.1 What is coherent

Naming is mostly disciplined:

- `*State` for state
- `*Handler` for input workflows
- `*Persistence` for persistence services
- `*View` for UI views
- `*FeatureModule` / `*Assembly` for standalone composition

This is a good base.

### 5.2 What is not yet explicit enough

The standalone composition layer still lacks a strict vocabulary for runtime-bearing modules.

Current ambiguity:

- some modules are "bindings + overlays"
- some are "presenters + handlers"
- some now also own live runtime services

But this is not encoded clearly in naming or interfaces.

Example:

- `SequencerFeatureModule` now owns a real runtime service and requires per-frame `update()`
- `MacroFeatureModule` currently does not
- nothing in the naming itself communicates this asymmetry

Required improvement:

- establish an explicit contract for modules that require runtime ticking
- make this visible in interfaces and assembly code

Current state after CA-003:

- runtime-bearing feature modules implement `StandaloneRuntimeFeature`
- `StandaloneFeatureAssembly` owns the explicit non-owning registry of runtime-bearing modules
- `StandaloneContext::update()` now ticks the registry through `updateRuntimeFeatures()`
- runtime ownership remains local to the feature module that owns the underlying service

### 5.3 Recommended naming rule

Use one consistent split:

- `*State`: authoritative data only
- `*Workflow` / `*DomainServices`: pure business rules and mutations
- `*Handler`: input-triggered workflows and binding setup
- `*Presenter` / `*ViewModelBuilder`: state-to-UI projection preparation
- `*View` / `*Widget`: LVGL-facing rendering only
- `*RuntimeService`: subsystems with clock/event/update ownership
- `*Assembly`: composition root only, no hidden behavior

Rule:

- if a class needs `update()`, it should be named or wrapped in a way that makes runtime ownership explicit

### 5.4 Placement rules by directory

These placement rules should now be treated as reproducible architecture constraints.

#### `src/state`

Allowed:

- authoritative state structs
- state lifecycle/orchestration
- persistence triggering decisions
- invariants over state transitions

Forbidden:

- LVGL calls
- input binding setup
- direct transport/MIDI hardware ownership

#### `src/handler`

Allowed:

- input binding setup
- translation of user actions into state/domain operations
- calls into narrow domain services or workflows

Forbidden:

- LVGL calls
- direct widget mutation
- persistence serialization details

#### `src/sequencer`

Allowed:

- playback runtime
- transport/clock ownership
- timing-sensitive sequencing services
- runtime/event integration for sequencer progression

Forbidden:

- LVGL object ownership
- standalone view composition

#### `src/context`

Allowed:

- composition roots
- module assembly
- scope wiring
- explicit lifecycle and update wiring

Forbidden:

- hidden business logic
- silent runtime ownership not visible in the composition contract

#### `src/ui`

Allowed:

- LVGL object creation
- rendering from state/view-models
- local UI geometry and cache logic

Forbidden:

- business-state transitions for convenience
- protocol/runtime decisions

#### `src/persistence`

Allowed:

- payload encoding/decoding
- slot storage orchestration
- storage integrity/version checks

Forbidden:

- view logic
- input logic
- cross-domain business workflows beyond persistence responsibilities

### 5.5 Naming rules for new code

Use these names consistently:

- `*State`: canonical data and lightweight invariants
- `*Lifecycle`: state lifecycle and periodic persistence/update orchestration
- `*Workflow`: business transitions that are not tied to one physical input gesture
- `*DomainServices`: narrow mutation/query façade for one domain
- `*Handler`: input-driven behavior and binding registration
- `*RuntimeService`: ticked or event-driven live subsystem
- `*Presenter`: state-to-overlay/view projection
- `*ViewModelBuilder`: non-LVGL render model preparation
- `*View` / `*Widget`: LVGL-facing rendering and object ownership
- `*Assembly`: composition only

Rule:

- never hide a `RuntimeService` behind a name that sounds purely static or declarative
- never place business workflows in `*View` or `*Widget`
- never place view mutation in `*Handler`

---

## 6. Dead Code / Inherited Code Cleanup Policy

### 6.1 Immediate rule

The codebase should not keep:

- historical files still referenced only by docs
- obsolete handler names in active docs
- transitional wrappers that exist only because of earlier refactors
- duplicate behavior split between old and new paths

### 6.2 Already validated cleanup targets

#### Documentation drift

These are not dead runtime code, but they are inherited artifacts that create confusion and should be treated as cleanup work:

- historical examples in `docs/STATE_MANAGEMENT.md`
- obsolete handler references in `docs/STANDALONE_INTERACTION_GRAMMAR_SPEC.md`
- architecture docs that still read as future recommendations where the code has already moved on

#### Composition ambiguity

The recent standalone runtime issue demonstrates that "implicitly live" services are unacceptable.

Policy:

- no runtime service may exist without an explicit owner and update path
- no feature module may quietly gain runtime behavior without exposing it in the assembly contract

### 6.3 Repository-wide cleanup rule

Before new feature work:

1. remove stale references to removed classes/files in docs
2. delete compatibility code that no longer has a caller
3. collapse duplicated pathways where one path is already authoritative

If a code path cannot be justified by a live caller or an explicit platform distinction, it should be removed.

---

## 7. Validated Codebase Risks by Layer

### 7.1 Context / Composition

Status: high risk

Why:

- explicit runtime update contract was missing until the recent fix
- feature ownership is not yet formalized strongly enough
- assemblies can still accumulate hidden integration responsibilities

Primary files:

- `src/context/StandaloneContext.cpp`
- `src/context/standalone/StandaloneFeatureAssembly.cpp`
- `src/context/standalone/StandaloneGlobalHandlerAssembly.cpp`
- `src/context/standalone/SequencerFeatureModule.cpp`
- `src/context/standalone/MacroFeatureModule.cpp`

### 7.2 State / Lifecycle

Status: medium risk

Why:

- authority is centralized, which is good
- but `CoreState` and `CoreStateLifecycle` now mediate many concerns
- future coupling risk is significant if feature code keeps reaching through the full root

Primary files:

- `src/state/CoreState.hpp`
- `src/state/CoreState.cpp`
- `src/state/CoreStateLifecycle.cpp`

### 7.3 Handlers

Status: medium-to-high risk in sequencer and macro

Why:

- sequencer handler logic was recently improved, but the structure-navigation and structure-edit workflows are still dense enough to require discipline
- macro handler logic was recently improved, but the structure and performance-mode workflows are still dense enough to require discipline
- macro and sequencer are converging, but not yet split into equally shaped responsibilities

Primary files:

- `src/handler/sequencer/SequencerStepHandler.cpp`
- `src/handler/sequencer/SequencerStructureNavigationWorkflow.cpp`
- `src/handler/sequencer/SequencerStructureEditWorkflow.cpp`
- `src/handler/macro/MacroPerformanceHandler.cpp`
- `src/handler/macro/MacroStructureWorkflow.cpp`
- `src/handler/macro/MacroPerformanceModeWorkflow.cpp`

### 7.4 UI

Status: medium risk

Why:

- recent UI convergence is strong
- but several render-heavy files are now large and geometry-dense
- the design intent is better than the local readability in some files

Primary files:

- `src/ui/view/SequencerView.cpp`
- `src/ui/sequencer/SequencerHeaderBar.cpp`
- `src/ui/common/TrackHeaderRow.cpp`
- `src/ui/common/TrackNavigationStrip.cpp`

### 7.5 Persistence

Status: low-to-medium risk

Why:

- structure is comparatively clear
- journal/slot logic is explicit
- versioning and payload sizes are declared
- biggest risk is not conceptual weakness but the cost of evolving payloads safely

---

## 8. Target Architecture Rules

These rules should now be treated as merge criteria, not only as guidance.

### Rule A: One live runtime = one explicit owner

Any subsystem that requires per-frame progression or event subscription must have:

- one clearly named owner
- one explicit update path
- one explicit cleanup path

No implicit runtime behavior.

### Rule B: Assemblies compose, they do not hide behavior

Assemblies may:

- construct modules
- wire scopes and dependencies
- expose explicit lifecycle/update calls

Assemblies should not:

- contain hidden business logic
- silently own feature behavior without surfacing it in their interface

### Rule C: Handlers must be split by interaction mode when they cross one screenful of logic

When a handler mixes:

- default navigation
- modal selection
- structural operations
- destructive/apply flows

it should be split by interaction responsibility, even if it still targets one view.

### Rule D: Docs must track the live system, not only the ideal one

If a file/class is removed or renamed, active docs must be updated in the same cleanup wave.

Historical guidance should be separated from current-contract guidance.

### Rule E: New contributors must navigate by stable seams

A new developer should be able to answer quickly:

- where is the authoritative state?
- where does runtime progression live?
- where do inputs bind?
- where is UI rendering performed?
- where is persistence triggered?

If a feature path cannot answer those questions cleanly, the architecture is not done.

---

## 9. Action Plan and Tracking Board

### 9.1 Board maintenance rules

This board must be updated whenever:

- an item changes status
- scope changes materially
- validation is completed
- a blocker is discovered
- work is paused for handoff

Minimum required board update at pause:

- `Status`
- `Last Updated`
- `Next Step`
- `Notes`

### 9.2 Work item template

Use this exact template for any new architecture item added later:

```text
ID:
Title:
Problem:
Scope:
Deliverables:
Validation:
Success Criteria:
Dependencies:
Status:
Last Updated:
Next Step:
Notes:
```

### 9.3 Active architecture workstreams

| ID | Title | Status | Dependencies | Primary Area | Success Evidence |
|---|---|---|---|---|---|
| CA-001 | Remove stale and inherited doc references | DONE | None | `docs/` | No active docs reference removed handlers/files |
| CA-002 | Separate historical guidance from current-contract docs | DONE | CA-001 | `docs/` | "current contract" docs contain only live naming and live paths |
| CA-003 | Formalize standalone runtime update contract | DONE | None | `src/context`, `src/context/standalone` | Runtime-bearing modules are explicitly identifiable and wired |
| CA-004 | Add standalone runtime integration coverage | DONE | CA-003 | `test/` | Test proves play toggle advances standalone playhead |
| CA-005 | Decompose `SequencerStepHandler` by interaction responsibility | DONE | CA-003 | `src/handler/sequencer` | No single handler owns the full selection/navigation/edit surface |
| CA-006 | Decompose `MacroPerformanceHandler` by interaction responsibility | DONE | CA-003 | `src/handler/macro` | Macro interaction concerns are split into clearer units |
| CA-007 | Narrow `CoreState` mutation surfaces in handlers/modules | DONE | CA-005, CA-006 | `src/state`, `src/handler`, `src/context` | Broad root access replaced by narrow refs/services where practical |
| CA-008 | Improve UI readability in large sequencer render files | DONE | CA-005 | `src/ui/sequencer`, `src/ui/view`, `src/ui/common` | Geometry/model logic is separated enough to reduce local reasoning cost |
| CA-009 | Remove dead or transitional code discovered during refactors | DONE | CA-001..CA-008 | repo-wide | Removed paths have no live caller and no active doc dependency |

### 9.4 Detailed work items

#### CA-001 - Remove stale and inherited doc references

Problem:

- active docs still reference removed or renamed handlers/workflows

Scope:

- `docs/STANDALONE_INTERACTION_GRAMMAR_SPEC.md`
- `docs/STATE_MANAGEMENT.md`
- any active docs referencing removed files/classes

Deliverables:

- docs updated to current names and current workflow seams

Validation:

- search active docs for known obsolete symbols and removed file references
- manually verify referenced files exist

Success Criteria:

- no active doc references removed handler/file names
- every referenced implementation path exists in the repo

Dependencies:

- none

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-003 by defining the smallest explicit runtime/update contract for standalone feature modules

Notes:

- cleaned `docs/STANDALONE_INTERACTION_GRAMMAR_SPEC.md` to use live standalone seams and live repo paths only
- labeled tutorial docs explicitly in `docs/STATE_MANAGEMENT.md` and `docs/HOW_TO_ADD_HANDLER.md`
- updated `docs/README.md` to separate normative references from illustrative guides
- validation executed with doc searches for obsolete handler names and stale absolute paths, plus manual existence checks for live handler links

#### CA-002 - Separate historical guidance from current-contract docs

Problem:

- some docs blend historical explanation and live implementation guidance

Scope:

- `docs/STATE_MANAGEMENT.md`
- architecture and extension docs that describe historical examples as if they were live norms

Deliverables:

- explicit split between historical/contextual material and current-contract guidance

Validation:

- each doc clearly declares whether it is historical, conceptual, or current-contract

Success Criteria:

- a new developer can tell which docs define today's contract without inference

Dependencies:

- CA-001

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- preserve the new doc labelling discipline while executing CA-003 and later items

Notes:

- `docs/README.md` now identifies current-contract references vs practical guides
- `docs/STATE_MANAGEMENT.md` and `docs/HOW_TO_ADD_HANDLER.md` now declare `Contract Level: Non-normative`
- `docs/STANDALONE_INTERACTION_GRAMMAR_SPEC.md` is now a current-contract baseline and includes an explicit historical note instead of mixing old and live behavior implicitly

#### CA-003 - Formalize standalone runtime update contract

Problem:

- runtime-bearing feature modules were not explicit enough in composition

Scope:

- `src/context/StandaloneContext.cpp`
- `src/context/standalone/StandaloneFeatureAssembly.*`
- `src/context/standalone/*FeatureModule*`

Deliverables:

- explicit contract/interface/pattern for modules requiring per-frame update
- composition layer documentation updated to reflect the rule

Validation:

- compile/build passes
- every runtime-bearing module can be located by contract, not only by reading implementation details

Success Criteria:

- no live runtime service exists without:
  - explicit owner
  - explicit update path
  - explicit cleanup path

Dependencies:

- none

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-004 by choosing the smallest standalone harness that can prove a registered runtime feature is actually ticked

Notes:

- added `src/context/standalone/StandaloneRuntimeFeature.hpp` as the explicit contract for runtime-bearing standalone modules
- added `src/context/standalone/StandaloneRuntimeFeatureRegistry.hpp` so the runtime ticking seam is shared between production code and tests
- `SequencerFeatureModule` now implements that contract and exposes `updateRuntime()`
- `StandaloneFeatureAssembly` now keeps an explicit runtime-feature registry and ticks it through `updateRuntimeFeatures()`
- `StandaloneContext::update()` now delegates to the explicit runtime registry instead of relying on an ad hoc module-specific `update()` convention
- build validation passed with `C:\\Users\\simon\\Documents\\ms-dev-env\\tools\\ninja\\ninja.exe -C C:\\Users\\simon\\Documents\\ms-dev-env\\.build\\core\\native -j 4`
- residual risk: the contract is now explicit in code, but still lacks a regression test that proves the runtime registry is exercised; that is CA-004

#### CA-004 - Add standalone runtime integration coverage

Problem:

- subsystem tests exist, but standalone assembly/runtime behavior is under-tested

Scope:

- standalone/native runtime path
- play/transport -> runtime -> playhead progression

Deliverables:

- at least one integration test covering standalone runtime progression

Validation:

- test fails before missing-runtime wiring and passes after correct wiring

Success Criteria:

- test proves that standalone play toggling produces playhead progression
- test would catch a future missing `update()` runtime regression

Dependencies:

- CA-003

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-005 with a responsibility inventory of `SequencerStepHandler`

Notes:

- added a temporary `test/test_StandaloneRuntimeFeatureRegistry/test_main.cpp` during the audit; this harness was later removed when CA-010 deleted the registry seam
- the test uses:
  - `StandaloneRuntimeFeature`
  - `StandaloneRuntimeFeatureRegistry`
  - a real `SequencerRuntimeService`
  - a mocked `oc::time::TimeProvider`
  - a real `oc::core::event::EventBus`
- the harness proves:
  - without runtime updates, `playheadStep` stays idle
  - once the runtime feature is registered and ticked, `playheadStep` advances
- validation executed with:
  - reference audit of the surviving standalone runtime owner/update sites after CA-010
  - `C:\\Users\\simon\\Documents\\ms-dev-env\\tools\\ninja\\ninja.exe -C C:\\Users\\simon\\Documents\\ms-dev-env\\.build\\core\\native -j 4`
- note for Windows: validation in this workspace should be executed sequentially to avoid intermittent `.pio\\build\\native` directory creation races

#### CA-005 - Decompose `SequencerStepHandler` by interaction responsibility

Problem:

- one handler currently owns too many interaction modes and state transitions

Scope:

- `src/handler/sequencer/SequencerStepHandler.cpp`
- related tests

Deliverables:

- decomposition plan implemented into smaller handler units or helper types with explicit responsibility seams

Validation:

- tests pass
- bindings still cover current behavior
- handler files are smaller and responsibility-focused

Success Criteria:

- navigation/focus logic is not interleaved with all structure mutation logic
- destructive/apply logic is not buried inside base navigation flow
- behavior remains unchanged from a user perspective

Dependencies:

- CA-003

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-006 with the same responsibility inventory applied to `MacroPerformanceHandler`

Notes:

- extracted `src/handler/sequencer/SequencerStructureNavigationWorkflow.*` for structure focus, preview, selection-mode entry/cancel, selection cursor movement, and previewed-structure creation
- extracted `src/handler/sequencer/SequencerStructureEditWorkflow.*` for erase/remove/copy/paste/delete/duplicate and hold-action behavior
- `SequencerStepHandler` is now a slimmer binding façade plus step-toggle behavior
- validation executed with:
  - `pio test -e native -f test_SequencerStepHandler`
  - reference audit of the surviving standalone runtime owner/update sites after CA-010
  - `C:\\Users\\simon\\Documents\\ms-dev-env\\tools\\ninja\\ninja.exe -C C:\\Users\\simon\\Documents\\ms-dev-env\\.build\\core\\native -j 4`
- behavioral coverage stayed green without changing the existing sequencer handler test harness
- note for Windows: `pio test` on the shared `native` environment must be launched sequentially, otherwise `.pio\\build\\native` can produce transient archive/build cleanup failures

#### CA-006 - Decompose `MacroPerformanceHandler` by interaction responsibility

Problem:

- macro interaction flow is converging with sequencer, but the handler is still dense and multi-modal

Scope:

- `src/handler/macro/MacroPerformanceHandler.cpp`
- related tests

Deliverables:

- clearer split between performance, quick controls, clutch, and structure-selection behavior

Validation:

- tests pass
- macro workflow remains behaviorally stable

Success Criteria:

- interaction concerns are separable and locally understandable
- naming matches the actual responsibilities

Dependencies:

- CA-003

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-007 by inventorying the broadest `CoreState` reach points still left in handlers and standalone feature modules

Notes:

- extracted `src/handler/macro/MacroStructureWorkflow.*` for page/track preview, navigation focus, selection mode, hold actions, and structure copy/paste/delete/duplicate flows
- extracted `src/handler/macro/MacroPerformanceModeWorkflow.*` for clutch, quick controls, and encoder mode/configuration behavior
- `MacroPerformanceHandler` is now a slimmer binding façade coordinating the two workflows
- validation executed with:
  - `pio test -e native -f test_MacroPerformanceHandler`
  - `pio test -e native -f test_SequencerStepHandler`
  - reference audit of the surviving standalone runtime owner/update sites after CA-010
  - `C:\\Users\\simon\\Documents\\ms-dev-env\\tools\\ninja\\ninja.exe -C C:\\Users\\simon\\Documents\\ms-dev-env\\.build\\core\\native -j 4`
- behavioral coverage stayed green without changing the existing macro handler test harness
- macro and sequencer do not need identical class shapes, but they now follow the same handler-as-binding-facade grammar

#### CA-007 - Narrow `CoreState` mutation surfaces in handlers/modules

Problem:

- broad `CoreState` access increases coupling and makes local reasoning harder

Scope:

- handlers and feature modules that currently reach deep into root state

Deliverables:

- narrower `StateRefs`
- targeted service façades where appropriate

Validation:

- diff of handler/module dependencies before vs after
- no loss of authority or behavior clarity

Success Criteria:

- handlers mostly consume narrow refs or domain services
- `CoreState` remains authoritative but is no longer the default grab-bag dependency

Dependencies:

- CA-005
- CA-006

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-008 by extracting the highest-value geometry/model seams from `SequencerHeaderBar.cpp`

Notes:

- added `src/handler/common/SharedTrackDomainServices.*` as the narrow shared-track mutation/query façade for sequencer structure workflows
- `SequencerStructureNavigationWorkflow` and `SequencerStructureEditWorkflow` now consume that façade instead of `CoreState`
- `SequencerRuntimeService` now declares explicit `StateRefs` and no longer depends on `CoreState`
- `SequencerFeatureModule` now receives only the sequencer runtime slice it needs plus `SharedTrackDomainServices` for step-handler wiring
- validation executed with:
  - `pio test -e native -f test_SequencerStepHandler`
  - reference audit of the surviving standalone runtime owner/update sites after CA-010
  - `pio test -e native -f test_MacroPerformanceHandler`
  - `C:\\Users\\simon\\Documents\\ms-dev-env\\tools\\ninja\\ninja.exe -C C:\\Users\\simon\\Documents\\ms-dev-env\\.build\\core\\native -j 4`
- note for Windows: `pio test` on the shared `native` environment remained sensitive to concurrent runs; clean sequential execution was required for reliable evidence

#### CA-008 - Improve UI readability in large sequencer render files

Problem:

- render-heavy files encode too much geometry/model logic inline

Scope:

- `src/ui/sequencer/SequencerHeaderBar.cpp`
- `src/ui/view/SequencerView.cpp`
- shared track/header strip components

Deliverables:

- extraction of stable geometry/model calculations where it materially improves readability

Validation:

- build passes
- no render regression
- code review confirms render files now describe rendering more than policy

Success Criteria:

- UI files are easier to scan
- local helper/model types carry stable geometry decisions
- no business workflow moves into UI in the process

Dependencies:

- CA-005

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- continue CA-009 opportunistically by removing any transitional UI helpers or stale seams discovered while touching the remaining large render files

Notes:

- extracted `src/ui/sequencer/SequencerHeaderBarRenderModel.*` to hold stable top-row visual derivation, strip geometry, strip page projection, and cursor layout calculations
- `SequencerHeaderBar.cpp` is now more clearly a LVGL render/apply file rather than a mixed render-and-layout policy file
- validation executed with:
  - `pio test -e native -f test_SequencerStepHandler`
  - `C:\\Users\\simon\\Documents\\ms-dev-env\\tools\\ninja\\ninja.exe -C C:\\Users\\simon\\Documents\\ms-dev-env\\.build\\core\\native -j 4`
- the extraction intentionally stayed local to the header bar instead of inventing a generic UI geometry layer

#### CA-009 - Remove dead or transitional code discovered during refactors

Problem:

- cleanup work will likely surface wrappers, compatibility branches, and references that no longer have live value

Scope:

- repo-wide, but only when justified by live caller analysis

Deliverables:

- removed dead code
- docs updated with each removal wave

Validation:

- grep/reference audit shows no live caller
- docs updated in the same change wave

Success Criteria:

- deleted code is provably unneeded
- no "dead but harmless" paths remain just to avoid deciding

Dependencies:

- CA-001 through CA-008 as discoveries emerge

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- resume feature planning from the cleaned baseline; open a new architecture item only if a future refactor reveals another live dead-path cluster

Notes:

- removed dead `SequencerHeaderBarProps` fields that were no longer read by the header widget after the render-model extraction:
  - `activeTrack`
  - `addTrackIndex`
  - `focusingTrack`
  - `focusingPage`
  - `previewTrackAddSlot`
  - `trackSelectedMask`
  - `trackActivity`
  - `dimmed`
- removed dead `SequencerHeaderBar` implementation leftovers:
  - `VISIBLE_TRACK_COUNT`
  - private `STEPS_PER_PAGE`
  - private `STRIP_HEIGHT`
  - stored `spacer_` member that had no read path after UI creation
- updated `src/ui/sequencer/SequencerViewModelBuilder.cpp` so it no longer computes header-only values that the widget no longer consumes
- updated this audit to replace the stale `feature_assembly_->update()` wording with the live `updateRuntimeFeatures()` contract
- validation executed with:
  - `git grep -n "\\.activeTrack =\\|\\.addTrackIndex =\\|\\.focusingTrack =\\|\\.focusingPage =\\|\\.previewTrackAddSlot =\\|\\.trackSelectedMask =\\|\\.trackActivity =\\|\\.dimmed =" -- src/ui/sequencer test`
  - `git grep -n "VISIBLE_TRACK_COUNT\\|spacer_" -- src/ui/sequencer docs`
  - reference audit of the surviving standalone runtime owner/update sites after CA-010
  - `C:\\Users\\simon\\Documents\\ms-dev-env\\tools\\ninja\\ninja.exe -C C:\\Users\\simon\\Documents\\ms-dev-env\\.build\\core\\native -j 4`
- code should not be kept because it once represented a previous direction

### 9.5 Execution protocol for every item

Before starting an item:

1. mark it `IN_PROGRESS`
2. record `Last Updated`
3. refine `Next Step` to the immediate next code change
4. confirm dependencies are `DONE`

During implementation:

1. keep changes inside the item scope
2. update docs when naming or placement changes
3. add or update tests when contracts change

Before marking `DONE`:

1. run the validation listed in the item
2. record any residual risk in `Notes`
3. update downstream docs if file/class names changed

At pause or handoff:

1. set the exact status
2. record the next concrete command or file to touch
3. record blockers and partial validation status

### 9.6 Merge gate for architecture work

An architecture item is not merge-ready unless:

- the code reflects the stated deliverable
- the stated validation has been executed
- the success criteria can be checked by another developer without oral context
- documentation has been updated where naming/placement/contracts changed
- the next item dependency chain remains clear

---

## 10. Priority Summary

Recommended execution order:

1. documentation and stale-reference cleanup
2. standalone runtime contract formalization
3. handler decomposition
4. narrower state/service dependency surfaces
5. UI readability cleanup
6. then resume larger feature expansion

Reason:

The current codebase is already converging well at the system level.
The main threat is no longer "wrong direction".
The main threat is "correct direction with too much local complexity and too much implicit assembly logic".

### 10.1 Safe resumption order

If work stops and later resumes, restart in this order:

1. complete the current `IN_PROGRESS` item if one exists
2. otherwise pick the first `TODO` item whose dependencies are all `DONE`
3. do not jump ahead to feature work while any high-risk architecture item remains open

### 10.2 Work that is explicitly unsafe to resume before cleanup

The following should not expand significantly before the architecture items above progress:

- new standalone runtime-bearing feature modules
- additional sequencer interaction modes added into `SequencerStepHandler` or its extracted structure workflows
- new cross-domain state mutations routed through broad `CoreState` access
- new UI feature layers added on top of already oversized sequencer render files

---

## 11. Final Assessment

Current assessment:

- architecture direction: strong
- product coherence trajectory: strong
- code quality: good but uneven
- maintainability: acceptable now, risky if complexity concentration continues
- onboarding readiness: not yet good enough for fast, low-friction entry

Conclusion:

`core` is not in need of a conceptual reset.
It needs explicit contracts, targeted decomposition, removal of stale inherited material, and stronger integration guarantees around standalone composition.

Once those are in place, feature work can resume on a much more reliable base.
