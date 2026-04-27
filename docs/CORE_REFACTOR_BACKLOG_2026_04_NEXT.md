# Core Refactor Backlog - Next Wave

> Date: 2026-04-16
> Scope: `midi-studio/core`
> Status: Working backlog for the next cleanup/refactor wave
> Purpose: continue the architecture cleanup without regressing the currently working standalone sequencer runtime path

---

## 1. Objective

This document is the execution backlog for the next refactor wave after
[CORE_ARCHITECTURE_AUDIT_2026_04.md](CORE_ARCHITECTURE_AUDIT_2026_04.md).

It keeps the same execution style:

- explicit work items
- reproducible validation
- restart-safe handoff
- cleanup work tracked with the same rigor as runtime work

This wave is not a conceptual reset.
It is a consolidation pass focused on ownership clarity, UI/runtime boundaries,
macro-domain cleanup, and removal of dead or ambiguous code.

### 1.1 Explicit constraints for this wave

These constraints are part of the contract for this backlog:

- the standalone sequencer runtime may stay allocated in PSRAM when built on Teensy 4.1
- the priority is runtime isolation and predictable ownership, not dogmatic relocation to RAM1/RAM2
- UI/render work must not become a hidden dependency of sequencer runtime progression or MIDI output
- dead, futile, or ambiguous stale code must be removed, not preserved behind compatibility seams
- a new developer must be able to identify the authoritative path for each concern without guesswork

### 1.2 Non-goals

This wave is not primarily about:

- moving the working sequencer runtime out of PSRAM just because it is hot
- rewriting the whole realtime path without measurement
- introducing a new generic framework layer for every repeated pattern
- preserving dead code "in case it becomes useful later"

If a larger runtime redesign becomes necessary later, it must be justified by
measured limitations, not by architectural aesthetics alone.

### 1.3 Status vocabulary

All tracked items in this document must use one of these statuses:

- `TODO`
- `IN_PROGRESS`
- `BLOCKED`
- `DONE`
- `CANCELLED`

Rule:

- at most one major item should be `IN_PROGRESS` in this backlog unless the write scopes are explicitly non-overlapping

### 1.4 Mandatory tracking fields

Each item must define:

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
- `Notes`

If one field is missing, the item is not execution-ready.

### 1.5 Resume protocol

When work resumes after interruption:

1. read sections 1, 3, 4, and 5 first
2. identify all items not marked `DONE` or `CANCELLED`
3. resume the highest-priority item whose dependencies are already satisfied
4. update that item's `Status`, `Last Updated`, and `Next Step` before further code changes
5. at pause or handoff, record:
   - current status
   - code touched
   - tests run
   - blockers
   - exact next action

---

## 2. Refactor posture

### 2.1 What must be preserved

The current standalone sequencer path already has value and should not be destabilized casually:

- the sequencer runtime works in standalone
- the current perf balance is acceptable
- the previous refactor already improved the separation between runtime progression and UI work
- the codebase has an established architecture vocabulary that should be refined, not replaced

### 2.2 What must be improved

This next wave exists because several important seams are still too implicit:

- standalone runtime ownership is not singular enough
- the current realtime contract is only partially explicit
- `MacroView` still performs domain transitions during activation
- `MacroDomainServices` remains too broad
- cold and fixed data still uses avoidable dynamic allocation in some paths
- dead or transitional code can still survive too long after refactors

### 2.3 Decision rule for this wave

Whenever there is tension between "keep the working current runtime behavior" and
"achieve cleaner ownership and lower onboarding friction":

- keep the working timing behavior
- improve ownership clarity
- remove ambiguity
- avoid speculative runtime rewrites unless measurement forces them

---

## 3. Target rules for the next wave

These rules are merge criteria for this backlog.

### Rule A: One standalone sequencer runtime = one live owner

For the standalone path, `SequencerRuntimeService` must have:

- one authoritative owner
- one explicit update path
- one explicit cleanup path

The runtime may live in PSRAM, but it may not exist in duplicate.

### Rule B: Realtime isolation is defined by coupling, not by storage location

For this project, the key rule is:

- UI CPU work may lag
- UI CPU work may not silently perturb sequencer runtime progression or MIDI output

This means the code must make the runtime lane explicit and reviewable, even if
the currently working implementation is kept.

### Rule C: Views render; lifecycle/domain transitions live elsewhere

`ui/view/*` may:

- show/hide LVGL objects
- bind watchers/subscriptions
- render precomputed projections

`ui/view/*` may not:

- synchronize domain runtime state for convenience
- write business-facing status state during activation
- hide behavior that a reader would expect in a workflow, feature module, or lifecycle seam

### Rule D: Domain services must be narrow enough to explain themselves

A domain service should expose one coherent slice of responsibility.

Reject:

- a service that mixes structure mutation, runtime value changes, config edits, and status feedback pulses unless the coupling is truly inseparable

Accept:

- smaller services or workflows that make the business transition visible at the call site

### Rule E: Dead or futile code is a bug in maintainability

If code is:

- dead
- transitional
- duplicated without a live distinct purpose
- dynamically allocated even though it is fixed, cold, and static in meaning

it should be removed or simplified in the same refactor wave.

### Rule F: New contributors must follow one path per concern

For each feature path, a new developer should be able to answer quickly:

- where does runtime ownership live?
- where do inputs bind?
- where do domain transitions happen?
- where does UI rendering happen?
- where is persistence triggered?

If more than one path looks authoritative, the architecture is not ready.

---

## 4. Action board

### 4.1 Board maintenance rules

This board must be updated whenever:

- an item changes status
- validation is completed
- scope changes materially
- a blocker is discovered
- work pauses for handoff

Minimum required update at pause:

- `Status`
- `Last Updated`
- `Next Step`
- `Notes`

### 4.2 Work item template

Use this exact template for any item added later:

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

### 4.3 Active workstreams

| ID | Title | Status | Dependencies | Primary Area | Success Evidence |
|---|---|---|---|---|---|
| CA-010 | Consolidate standalone sequencer runtime ownership | DONE | CA-003, CA-004 | `main.cpp`, `src/context/standalone`, `src/sequencer` | One authoritative runtime owner remains, with no duplicate live path |
| CA-011 | Formalize the current realtime lane contract without regressing the working path | DONE | CA-010 | `src/sequencer`, `docs/` | The current timer/runtime/UI split is explicit and reviewable |
| CA-012 | Remove macro-domain transitions from `MacroView` activation | DONE | None | `src/ui/view`, `src/context/standalone`, `src/state/macro` | Macro view activation becomes presentation-only |
| CA-013 | Narrow macro domain service surfaces | DONE | CA-012 | `src/handler/macro`, `src/state/macro`, `src/context/standalone` | Macro handlers depend on smaller, self-explanatory seams |
| CA-014 | Remove dead, futile, and ambiguous code in touched areas | DONE | CA-010..CA-013 as discoveries emerge | repo-wide, focused on touched seams | No dead or redundant path survives "for later" |
| CA-015 | Add regression coverage and handoff docs for the new seams | DONE | CA-010..CA-014 | `test/`, `docs/` | Ownership, activation, and narrowed-service seams are protected by tests/docs |

---

## 5. Detailed work items

### CA-010 - Consolidate standalone sequencer runtime ownership

Problem:

- `SequencerRuntimeService` is currently instantiated in more than one standalone-relevant place, which makes ownership ambiguous and opens the door to duplicate updates, duplicate subscriptions, and duplicate MIDI side effects

Scope:

- `main.cpp`
- `src/context/StandaloneContext.*`
- `src/context/standalone/StandaloneFeatureAssembly.*`
- `src/context/standalone/SequencerFeatureModule.*`
- related tests and docs

Deliverables:

- one authoritative standalone owner for `SequencerRuntimeService`
- one explicit init/update/cleanup path
- explicit documentation that this owner may stay in PSRAM on Teensy 4.1
- removal of the duplicate runtime path

Validation:

- `pio run -e dev`
- `pio test -e native`
- search for all `SequencerRuntimeService` construction sites after the refactor
- search for standalone runtime update hooks after the refactor

Success Criteria:

- exactly one live standalone `SequencerRuntimeService` instance remains
- standalone playhead progression still works
- no duplicate MIDI clock/start/stop path remains for the same standalone runtime
- runtime ownership is obvious from reading the composition seam

Dependencies:

- CA-003
- CA-004

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-011 by documenting the current realtime lane contract around the surviving `main.cpp` pre-context hook and `SequencerRuntimeService::onInternalTimer_()`

Notes:

- keeping the final authoritative runtime in PSRAM is acceptable for this wave
- the problem is duplicate ownership and ambiguity, not PSRAM by itself
- authoritative owner selected for implementation: the PSRAM runtime allocated in `main.cpp` and updated from the app pre-context hook
- delivered: removed the `SequencerFeatureModule` runtime instance, removed `StandaloneContext -> StandaloneFeatureAssembly -> runtime registry` ticking, and deleted the obsolete standalone runtime-feature scaffolding/test
- touched code: `main.cpp`, `src/context/StandaloneContext.*`, `src/context/standalone/SequencerFeatureModule.*`, `src/context/standalone/StandaloneFeatureAssembly.*`
- tests run: `pio test -e native`, `pio run -e dev -t clean`, `pio run -e dev`

### CA-011 - Formalize the current realtime lane contract without regressing the working path

Problem:

- the current standalone sequencer runtime behaves acceptably, but the architectural contract around timer work, playback work, UI publication, and MIDI output is still partly implicit
- this makes future refactors risky because reviewers can no longer tell which work is essential in the hot path and which work is merely tolerated today

Scope:

- `src/sequencer/SequencerRuntimeService.*`
- `src/sequencer/SequencerPlaybackService.*`
- `src/sequencer/MidiClockSyncService.*`
- `docs/REALTIME_MIDI_ISOLATION_PLAN.md`
- any adjacent contract docs needed to describe the current lane split

Deliverables:

- an explicit written contract for the current runtime lane
- a classification of timer-path work into:
  - required now
  - deferrable later
  - forbidden in future additions
- the smallest safe code cleanups that reduce ambiguity without destabilizing the working path
- a documented measurement gate for any larger future scheduler/output split

Validation:

- `pio run -e dev`
- `pio test -e native -f test_MidiClockSyncService`
- targeted review of `onInternalTimer_()` against the documented contract
- hardware/perf evidence captured only if code changes alter the hot path materially

Success Criteria:

- the current working runtime path is documented as an explicit contract, not tribal knowledge
- UI publication remains outside the hot runtime path
- reviewers can identify which timer-path work is tolerated today and which work must not expand there
- no speculative redesign is merged without measurement

Dependencies:

- CA-010

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-012 by extracting the macro activation side effects out of `MacroView::onActivate()` into an explicit standalone lifecycle owner

Notes:

- this item is about consolidation first, not a mandatory full queue/scheduler redesign in this wave
- if the current path remains measurably acceptable, clarity and guardrails have priority over churn
- delivered: documented the current control-lane / timer-lane / projection-lane split in `REALTIME_MIDI_ISOLATION_PLAN.md`
- delivered: classified `onInternalTimer_()` work into required, deferrable, and forbidden categories
- delivered: renamed `publishPlaybackUiFromRealtime_()` to `publishPlaybackUiFromTimerPath_()` and added guardrail comments around the surviving timer lane
- touched code: `src/sequencer/SequencerRuntimeService.*`, `docs/REALTIME_MIDI_ISOLATION_PLAN.md`
- tests run: `pio test -e native -f test_MidiClockSyncService`, `pio run -e dev`

### CA-012 - Remove macro-domain transitions from `MacroView` activation

Problem:

- `MacroView::onActivate()` currently performs domain/runtime synchronization and status-bar writes, which hides business behavior in the UI layer

Scope:

- `src/ui/view/MacroView.*`
- `src/context/standalone/MacroFeatureModule.*`
- `src/state/macro/MacroWorkflow.*`
- any lifecycle/helper seam needed to relocate the activation work

Deliverables:

- an explicit owner for macro activation-side domain work outside `ui/view/*`
- `MacroView` activation reduced to presentation lifecycle only
- the current user-visible behavior preserved

Validation:

- `pio run -e dev`
- `pio test -e native -f test_ActiveViewLifecycle`
- `pio test -e native -f test_MacroPerformanceDomainServices`
- `pio test -e native -f test_MacroPerformanceHandler`
- grep/review to confirm `MacroView.cpp` no longer performs domain mutations on activation

Success Criteria:

- `MacroView` no longer synchronizes runtime macro state during activation
- `MacroView` no longer writes `statusBar.pageName` during activation
- the activation owner is explicit and readable from the composition/lifecycle seam
- user-visible macro activation behavior remains stable

Dependencies:

- none

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-013 by inventorying `MacroDomainServices` responsibilities and grouping them into narrower seams that match the existing macro workflows

Notes:

- do not move the hidden behavior to another arbitrary place
- the new owner must be a seam a new developer would naturally inspect
- delivered: `MacroView::onActivate()` is now presentation-only
- delivered: macro activation-side domain work moved into `StandaloneContext::prepareMacroViewActivation()`
- delivered: the new owner sits in the same lifecycle seam that already orchestrates active view switching
- touched code: `src/ui/view/MacroView.cpp`, `src/context/StandaloneContext.*`
- tests run: `pio test -e native -f test_ActiveViewLifecycle`, `pio test -e native -f test_MacroPerformanceDomainServices`, `pio test -e native -f test_MacroPerformanceHandler`, `pio run -e dev`

### CA-013 - Narrow macro domain service surfaces

Problem:

- `MacroDomainServices` still concentrates unrelated concerns:
  - runtime values
  - config mutation
  - page/track switching
  - structure edits
  - status feedback pulses
- that breadth keeps macro handlers too coupled to an oversized implicit surface

Scope:

- `src/handler/macro/MacroDomainServices.*`
- macro handlers and workflows that depend on it
- adjacent macro workflows/state helpers when extraction is needed

Deliverables:

- a target responsibility map for macro-domain seams before implementation
- smaller services and/or workflows with stable names and stable responsibility boundaries
- handlers updated to depend only on the surfaces they actually need

Validation:

- `pio run -e dev`
- `pio test -e native -f test_MacroPerformanceDomainServices`
- `pio test -e native -f test_MacroEditHandler`
- `pio test -e native -f test_MacroPerformanceHandler`
- dependency review of macro handlers before/after

Success Criteria:

- no single macro service mixes structure, config, runtime, and feedback responsibilities without strong justification
- macro handlers can be understood locally with fewer hidden cross-domain assumptions
- `CoreState` remains authoritative, but macro handlers no longer consume it through one oversized façade

Dependencies:

- CA-012

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-014 by deleting the retired façade, aligning tests to the surviving seams, and removing any dead coupling discovered in the touched macro path

Notes:

- avoid replacing one god-service with a generic abstraction blob
- prefer domain-specific seams whose names explain the mutation they own
- delivered: split the old macro façade into `MacroEditDomainServices`, `MacroPerformanceDomainServices`, and `MacroStructureDomainServices`
- delivered: updated `MacroEditHandler`, `MacroValueHandler`, `MacroMidiHandler`, `MacroPerformanceHandler`, `MacroPerformanceModeWorkflow`, `MacroStructureWorkflow`, `MacroFeatureModule`, and standalone assembly wiring to depend on narrower seams
- delivered: moved preview resynchronization ownership out of the domain service and into `MacroStructureWorkflow` / explicit test setup where appropriate
- delivered: deleted `src/handler/macro/MacroDomainServices.*` and replaced the direct façade test with `test_MacroPerformanceDomainServices`
- touched code: `src/handler/macro/*`, `src/context/standalone/MacroFeatureModule.*`, `src/context/standalone/StandaloneFeatureAssembly.cpp`, `test/test_MacroEditHandler/test_main.cpp`, `test/test_MacroPerformanceHandler/test_main.cpp`, `test/test_MacroPerformanceDomainServices/test_main.cpp`
- tests run: `pio test -e native -f test_MacroPerformanceDomainServices`, `pio test -e native -f test_MacroEditHandler`, `pio test -e native -f test_MacroPerformanceHandler`, `pio run -e dev`

### CA-014 - Remove dead, futile, and ambiguous code in touched areas

Problem:

- refactors keep surfacing cold dynamic allocations, transitional helpers, and duplicate paths that no longer have a real architectural reason to exist
- leaving them behind increases friction for the next contributor

Scope:

- any files touched by CA-010 through CA-013
- docs that reference touched seams
- fixed cold data structures that still allocate dynamically without need

Deliverables:

- dead/transitional code removed in the same change wave that makes it obsolete
- fixed cold data switched to static or flash-friendly forms where appropriate
- docs updated alongside cleanup

Validation:

- grep/reference audit showing no live caller remains for deleted paths
- `pio run -e dev`
- relevant targeted `pio test -e native -f ...` commands for touched areas

Success Criteria:

- no dead or duplicate code remains "for later"
- no dynamic global container remains for fixed cold labels without explicit justification
- touched areas expose one clear authoritative path per concern

Dependencies:

- CA-010 through CA-013 as discoveries emerge

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- start CA-015 by tightening the remaining regression coverage/docs around the new macro seams and the singular standalone runtime owner

Notes:

- cleanup is not optional polish for this wave
- if a path becomes dead during the refactor, delete it before calling the item done
- delivered: removed the retired `MacroDomainServices` path in the same wave that introduced the narrower macro seams
- delivered: replaced the standalone view-selector's global `std::vector<std::string>` with static string-view labels and a selector interface that no longer requires a global dynamic container for fixed cold items
- delivered: updated backlog validation/history references so the live test seam is `test_MacroPerformanceDomainServices`
- touched code: `src/context/standalone/StandaloneOverlayAssembly.cpp`, `../ui/src/ms/ui/widget/StringListSelector.*`, `docs/CORE_REFACTOR_BACKLOG_2026_04_NEXT.md`
- validation evidence: reference audit for `MacroDomainServices` in `src/`, `test/`, and `docs/`; `pio run -e dev`

### CA-015 - Add regression coverage and handoff docs for the new seams

Problem:

- the codebase already has good domain coverage, but the exact seams targeted in this next wave can still regress quietly if ownership, activation behavior, and service boundaries stay weakly tested/documented

Scope:

- `test/`
- `docs/README.md`
- any targeted docs needed to describe live ownership and activation contracts

Deliverables:

- regression coverage for the consolidated standalone sequencer runtime owner
- regression coverage or equivalent reviewable evidence for the macro activation contract
- docs index updated so the next contributor lands on the live backlog and contract docs first

Validation:

- `pio test -e native`
- `pio run -e dev`
- manual doc pass ensuring README entry points match the live codebase

Success Criteria:

- future regressions in these seams are easier to catch than they are today
- the docs index points clearly to the live backlog and current architecture references
- a new developer can resume the next wave without oral context

Dependencies:

- CA-010 through CA-014

Status:

- DONE

Last Updated:

- 2026-04-16

Next Step:

- refactor wave complete; start a new backlog item only if a new architecture problem appears beyond the CA-010..CA-015 baseline

Notes:

- do not add broad low-signal tests just to increase count
- prefer a few high-value regression tests plus exact docs over many shallow checks
- delivered: added `test/test_StandaloneSequencerRuntimeGate` to lock the pre-context runtime gate contract used by `main.cpp`
- delivered: added `test/test_MacroViewActivationContract` to lock the macro activation-side domain sync outside `ui/view/*`
- delivered: kept `test/test_ActiveViewLifecycle` as the lifecycle ordering check and added a standalone lifecycle handoff doc
- delivered: introduced `src/context/standalone/StandaloneSequencerRuntimeGate.hpp` and `src/context/standalone/MacroViewActivationContract.hpp` as explicit reviewable seams for the two previously implicit contracts
- delivered: updated `docs/README.md` so the next contributor lands on the backlog and standalone lifecycle contract earlier
- touched code: `main.cpp`, `src/context/StandaloneContext.*`, `src/context/standalone/StandaloneSequencerRuntimeGate.hpp`, `src/context/standalone/MacroViewActivationContract.hpp`, `test/test_StandaloneSequencerRuntimeGate/test_main.cpp`, `test/test_MacroViewActivationContract/test_main.cpp`, `docs/STANDALONE_LIFECYCLE_CONTRACT.md`, `docs/README.md`
- tests run: `pio test -e native -f test_StandaloneSequencerRuntimeGate`, `pio test -e native -f test_MacroViewActivationContract`, `pio test -e native -f test_ActiveViewLifecycle`, `pio test -e native`, `pio run -e dev`

---

## 6. Recommended execution order

Recommended sequence:

1. CA-010
2. CA-011
3. CA-012
4. CA-013
5. CA-014
6. CA-015

Reason:

- singular runtime ownership is the highest-value ambiguity to eliminate first
- the realtime lane contract should be consolidated after the owner is singular
- macro UI purity should be restored before shrinking macro service surfaces
- cleanup should happen continuously, but CA-014 formalizes it as a merge gate
- coverage and docs should close the wave, not trail it indefinitely

### 6.1 Explicitly unsafe shortcuts

Do not do these:

- move runtime code between memory regions as a side effect of CA-010
- rewrite the timer path wholesale before CA-011 classifies current hot-path work
- leave `MacroView` activation side effects in place while claiming CA-012 is "mostly done"
- split `MacroDomainServices` without first naming the target seams
- keep duplicate or dead code because deletion feels risky

---

## 7. Exit condition for this wave

This backlog is complete only when all of the following are true:

- standalone sequencer runtime ownership is singular and explicit
- the current working realtime lane contract is documented and reviewable
- macro view activation is presentation-only
- macro handlers depend on narrower, clearer domain seams
- dead or futile code discovered in the wave has been removed
- tests/docs provide restart-safe context for the next contributor

If one of these is still false, the wave is not done.
