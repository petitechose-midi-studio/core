# Architecture Chantiers

Purpose: list codebase-scale architecture chantiers that would reduce onboarding
friction, remove ambiguity, and improve maintainability.

This is not an implementation plan. Each chantier below names why it is useful,
what is already confirmed, what still needs verification, and the main risks and
mitigations before implementation work starts.

## Evidence Baseline

Primary exploration sources:

- The versioned docs and source files listed below are the current source of
  truth.
- Local exploration notes may exist under `docs/_codex-exploration/`, but that
  directory is intentionally excluded from Git. Treat those notes as scratch
  evidence only: verify their claims against current source before using them to
  plan a change.

Current repo docs and source contracts:

- [`docs/README.md`](../README.md)
- [`docs/ARCHITECTURE_REVIEW_RULES.md`](../ARCHITECTURE_REVIEW_RULES.md)
- [`src/context/StandaloneContext.hpp`](../../src/context/StandaloneContext.hpp)
- [`src/context/standalone/StandaloneSequencerRuntimeGate.hpp`](../../src/context/standalone/StandaloneSequencerRuntimeGate.hpp)
- [`src/context/standalone/ActiveViewLifecyclePlan.hpp`](../../src/context/standalone/ActiveViewLifecyclePlan.hpp)
- [`src/sequencer/SequencerRuntimeService.hpp`](../../src/sequencer/SequencerRuntimeService.hpp)

Code seams checked during this review:

- [`main.cpp`](../../main.cpp)
- [`src/context/StandaloneContext.cpp`](../../src/context/StandaloneContext.cpp)
- [`src/state/CoreState.hpp`](../../src/state/CoreState.hpp)
- [`src/state/CoreState.cpp`](../../src/state/CoreState.cpp)
- [`src/state/CoreStateLifecycle.cpp`](../../src/state/CoreStateLifecycle.cpp)
- [`src/sequencer/SequencerRuntimeService.cpp`](../../src/sequencer/SequencerRuntimeService.cpp)
- [`src/sequencer/SequencerInternalTimerLane.cpp`](../../src/sequencer/SequencerInternalTimerLane.cpp)
- [`src/sequencer/RealtimeMidiQueue.cpp`](../../src/sequencer/RealtimeMidiQueue.cpp)

## Certainty Levels

### Confirmed In Current Code

- `CoreState` is the central state authority and lifecycle hub.
  Evidence: `src/state/CoreState.hpp`, `src/state/CoreState.cpp`,
  `src/state/CoreStateLifecycle.cpp`, plus the include/fan-in maps.
- The current standalone sequencer runtime owner is `main.cpp`, through a
  pre-context update hook. `StandaloneContext::update()` does not tick it.
  Evidence: `main.cpp`, `src/context/StandaloneContext.cpp`,
  `src/context/StandaloneContext.hpp`,
  `src/context/standalone/StandaloneSequencerRuntimeGate.hpp`, and
  `src/sequencer/SequencerRuntimeService.hpp`.
- The main domains already have intended extension seams: macro workflows and
  services, sequencer snapshot/track-bank operations, Data Manager workflow,
  persistence workflows, and UI view-model builders.
  Evidence: current source contracts and the source files named in each sprint
  plan. Local domain maps under `docs/_codex-exploration/domain-*.md`, when
  present, are secondary navigation aids.
- There is a meaningful native test surface by domain.
  Evidence: run `ms test core` and inspect the `test/` directories for the
  relevant domain.
- There is real documentation drift.
  Evidence: the Sprint 0 pre-cleanup scan found a missing action-strip spec
  link and older runtime-registry wording that no longer matched the current
  hook-based runtime owner.

### Needs Verification Before Being Treated As Fact

- Whether a live integration test currently proves:
  standalone play toggle -> runtime update -> playhead progression.
  The runtime gate test exists, but the full playhead-progression test was not
  confirmed during this pass.
- Whether every remaining `CoreState&` dependency outside composition/state
  code is justified, or whether some should become narrower `StateRefs` or
  domain services.
- The semantic conflict behavior of every input binding and overlay predicate.
  The bindings are lexically mapped, but `.when(...)` predicates and latch/scope
  interactions still need state-machine review or targeted tests.
- Hardware behavior under load: Teensy timer lane, USB MIDI output pressure, SD
  write latency, display flush timing, and MUX/input behavior.
- Persistence binary compatibility with data produced by older firmware.
- UI visual correctness and frame pacing under SDL/LVGL or target hardware.

## Recommended Sprint Order

## Todo To Sprint Mapping

This mapping is the current routing table for the codebase-scale discovery work.
It exists to keep each sprint scoped and to avoid mixing documentation cleanup,
state-surface reduction, modal workflow review, shared structure mechanics,
persistence policy, and UI validation in a
single change wave.

Status as of 2026-04-29:

- Sprint 0 is complete for the tracked documentation entry path.
- Sprint 1 is complete for the scoped Gates 1-6 implementation tranche.
- Sprint 2 is complete for the scoped handler-review tranche.
- Sprint 3 is complete for the scoped shared-structure tranche.
- Sprint 4 is complete for the software compatibility/failure-semantics and
  Teensy main-loop recovery-wiring tranche; hardware SD hot-swap validation
  remains future work.
- Sprint 5 has started: the native SDL app has a repeatable BMP capture path
  for main screens and high-risk overlays.

| Todo | Sprint | Why it belongs there |
|---|---|---|
| Finalize and validate this architecture-chantier portfolio | Sprint 0 | Complete; it defines the source-of-truth entry point for the rest of the work. |
| Audit active docs for dead links, obsolete seams, and historical/current-contract confusion | Sprint 0 | Complete for tracked docs; repeat after doc moves or renames. |
| Update docs index/results and retire misleading docs | Sprint 0 | Complete; onboarding now starts from `docs/README.md`. |
| Reduce broad `CoreState&` / `fromCoreState(...)` usage outside authority/composition | Sprint 1 | Complete for Gates 1-6; keep future changes within the documented access policy. |
| Formalize shared-track authority | Sprint 1 | Complete through `SharedTrackCoordinator`. |
| Convert low-risk UI projections away from `CoreState` | Sprint 1 | Complete for `GlobalTrackNavigationStripModel`; use the same focused-source pattern for future projections. |
| Build the input/overlay binding state-machine matrix | Sprint 2 | Complete for the scoped handler-review tranche; future matrix findings can add focused regressions. |
| Add targeted tests for critical input conflicts | Sprint 2 | Complete for the named Sprint 2 handler surfaces. |
| Review shared page/track structure mechanics | Sprint 3 | Complete for scoped tranche: shared slot primitives, duplicate, track copy/paste, and sequencer track creation helper are covered. |
| Inventory persistence formats and compatibility needs | Sprint 4 | Software tranche complete: active storage domains, versions, sizes, compatibility promises, and status semantics are documented. |
| Decide SD failure/hot-swap policy | Sprint 4 | Software tranche complete: RAM-authoritative recovery is documented, covered by tests, and wired in the Teensy main loop; hardware validation remains future work. |
| Capture SDL/LVGL screens and overlays | Sprint 5 | This validates visual UI behavior. |
| Produce a minimal UI visual reference report | Sprint 5 | This makes UI regressions reviewable. |
| Verify standalone runtime playhead progression and hardware timing | Realtime validation | Runtime/hardware proof remains required, but it is not the current Sprint 1 scope. |
| Audit only external dependencies traversed by `core` | Cross-sprint validation | This is targeted dependency validation after local contracts are clearer. |
| Convert validated discoveries into implementation tickets | Cross-sprint closure | Tickets should be produced continuously after each sprint, then consolidated. |

Detailed sprint plans:

- [Sprint 0: Documentation Source Of Truth](sprint-0-documentation-source-of-truth.md)
- [Sprint 1: CoreState Domain Boundaries Analysis](sprint-1-corestate-domain-boundaries-analysis.md)
- [Sprint 2: Input And Overlay State-Machine Recognition](sprint-2-input-overlay-state-machine-recognition.md)
- [Sprint 3: Shared Structure Mechanics Recognition](sprint-3-shared-structure-mechanics-recognition.md)
- [Sprint 4: Persistence Compatibility And Failure Semantics](sprint-4-persistence-compatibility-failure-semantics.md)
- [Sprint 5: UI Visual Validation And Render Maintainability](sprint-5-ui-visual-validation.md)
- [HPP Contract Compliance Map](hpp-contract-compliance-map.md)
- [HPP Contract Compliance Tracker](hpp-contract-compliance-tracker.md)

### Sprint 0: Documentation Source Of Truth

Goal: make the docs tell one current story.

Why this is a good idea:

- New contributors currently have to reconcile active docs, historical audit
  notes, refactor backlog entries, and exploration maps.
- A stale or contradictory doc is a direct architecture risk because it can send
  implementation work toward an obsolete seam.

Evidence:

- The pre-cleanup `docs/README.md` had a broken action-strip spec reference.
- The pre-cleanup `docs/README.md` presented illustrative guides beside current
  contracts.
- Source checks show runtime ownership is currently in `main.cpp`, while older
  audit wording mentioned a removed runtime registry/update path.

Risks:

- Removing legacy docs can erase useful rationale if the rationale has not been
  moved into the live code contract.
- Over-editing docs before all code facts are verified can create a new false
  authority.

Mitigations:

- Do not keep visible legacy documentation as a normal entry point. If a
  historical rationale is still useful, move the minimal "why" into the relevant
  `.hpp` contract comment or a short current-contract doc, then retire the
  legacy document.
- Promote only docs with current file/symbol evidence into "current contract"
  status.
- Replace stale links with either a live target or an explicit "removed/retired"
  note.
- Keep `.cpp` comments minimal and local to non-obvious implementation details.

Exit signal:

- A reader can identify the current contracts and knows which docs are
  current within five minutes, without seeing legacy docs in the standard entry
  path.

### Sprint 1: `CoreState` Surface Reduction And Domain Boundaries

Goal: keep `CoreState` as the private ownership/lifecycle root while removing it
as the normal feature-facing API.

Why this is a good idea:

- A central authority is useful, but wide mutable access makes local reasoning
  harder.
- The codebase already has a positive pattern: many handlers/modules consume
  `StateRefs` or domain services. The sprint should standardize when each shape
  is appropriate.
- Reducing the broad state surface now lowers the friction of adding features
  later, without paying the memory/performance risk of a full rewrite.

Evidence:

- Include maps show `src/state` is the highest internal include target.
- Existing code has many `StateRefs` seams, but also remaining `CoreState&`
  workflows and `fromCoreState(...)` factories.
- `sprint-1-corestate-domain-boundaries-analysis.md` classifies production
  `CoreState` access and defines progressive refactor gates.
- Native checks already cover `CoreState` lifecycle/persistence, shared track,
  Data Manager services, macro performance services, macro activation, and the
  standalone runtime gate.

Risks:

- Over-narrowing too soon can create noisy plumbing.
- Splitting state access without understanding invariants can hide cross-domain
  transitions instead of clarifying them.
- Leaving compatibility bridges behind after migration would create a second
  legacy layer.

Mitigations:

- First classify current usages: composition root, state lifecycle, legitimate
  cross-domain workflow, handler/service convenience, UI projection.
- Change only the categories that create real ambiguity.
- Keep cross-domain invariants named and reviewable rather than hidden behind
  generic helpers.
- Close each gate by removing old access paths that no longer have callers.

Exit signal:

- New contributors can tell which module owns a mutation without inspecting a
  broad `CoreState` object graph.
- `src/handler` and `src/ui` no longer expose broad `CoreState` dependencies
  except for composition-approved bridges.

### Sprint 2: Input And Overlay State-Machine Map

Goal: turn lexical binding knowledge into semantic interaction knowledge.

Why this is a good idea:

- The app has many modal input paths, latches, scopes, overlays, and `.when(...)`
  predicates.
- Most user-facing regressions in this kind of embedded UI are mode-conflict
  bugs, not isolated function bugs.

Evidence:

- `input-binding-map.md` extracted 107 fluent binding blocks.
- The same map explicitly says predicate behavior and conflict/priority outcomes
  are not semantically exhaustive.
- Handler files contain many overlapping bindings on NAV, bottom buttons, macro
  buttons, and overlay scopes.

Risks:

- A full formal state machine could be too expensive.
- Tests that encode current accidental behavior might freeze bad UX.

Mitigations:

- Start with high-risk modes only: macro performance, sequencer structure edit,
  Data Manager/global settings overlays, and view switching.
- Record intended behavior separately from observed behavior.
- Add targeted tests only for conflicts that are intended contracts.

Exit signal:

- A reader can answer "which handler owns this button in this mode?" without
  grep archaeology.

### Sprint 3: Shared Structure Mechanics

Goal: make page/track structure behavior easy to reason about across macro and
sequencer workflows.

Current status: complete for the scoped tranche. The current split between
`StructureSlotOps`, `SharedTrackCoordinator`, macro structure services, and
sequencer structure workflows is coherent. Sprint 3 adds direct shared slot
coverage, macro/sequencer duplicate and track copy/paste regressions, and a
narrow `SequencerStructureTrackOps.hpp` helper for the duplicated sequencer
track creation path; `ms test core` passes `44/44`.

Why this is a good idea:

- Macro page/track operations and sequencer structure operations share concepts:
  masks, active indices, copy/paste, delete, duplicate, and add-slot behavior.
- Shared mechanics should be obvious without blurring which domain owns each
  mutation.

Evidence:

- `MacroStructureDomainServices` coordinates page/track structure operations.
- Sequencer structure handlers and workflows own sequencer-side structure edits.
- Shared slot helpers already exist in `src/state/shared`.

Risks:

- Extracting shared mechanics too early can hide domain-specific behavior.
- Duplicated structure helpers can drift if left unclassified.

Mitigations:

- First document which operations are truly shared and which are domain-owned.
- Use the dedicated recognition note:
  [Sprint 3: Shared Structure Mechanics Recognition](sprint-3-shared-structure-mechanics-recognition.md).
- Add tests only for helpers that become shared contracts.
- Keep domain workflows responsible for persistence, status, and runtime side
  effects.

Exit signal:

- Shared structure helpers are reusable without making macro/sequencer ownership
  ambiguous.

### Sprint 4: Persistence Compatibility And Failure Semantics

Goal: clarify storage format compatibility and failure behavior.

Current status: software tranche complete. The active storage domains, versions,
payload sizes, failure semantics, and recovery policy are documented in
[Sprint 4: Persistence Compatibility And Failure Semantics](sprint-4-persistence-compatibility-failure-semantics.md).
`CoreSettings` now reports unavailable storage explicitly, pending macro/shared
writes survive transient unavailable storage, and native tests cover the
existing v1 settings compatibility path plus unavailable-storage behavior. The
hot-swap strategy is RAM-authoritative recovery: reopen/revalidate storage, then
persist current RAM workspaces/settings without auto-loading SD data into the
live session. The pure recovery state machine and `CoreState` recovery API are
implemented and covered by `ms test core`; the Teensy main-loop manager is wired
in `main.cpp` and the firmware build passes.

Why this is a good idea:

- Current persistence tests prove present-day roundtrips and corruption handling,
  but not necessarily compatibility with older firmware data.
- SD-card failure and hot-swap behavior are not proven by native tests.

Evidence:

- `domain-persistence.md` names missing binary compatibility audit and physical
  SD failure behavior.
- `hardware-target-map.md` documents six storage domains and SD backend behavior.
- `main.cpp` wires a main-loop recovery manager around `SDCardBackend::reopen()`.
- `src/persistence/StorageRecoveryMachine.hpp` and
  `CoreState::recoverPersistenceFromRamAfterStorageReopen()` provide the native
  foundation for that manager.

Risks:

- Format migration can become invasive if mixed with feature work.
- Over-engineering failure recovery can add complexity beyond current product
  needs.

Mitigations:

- First inventory payload versions and stored domains.
- Add fixtures for any released formats before changing codecs.
- Keep SD recovery policy explicit: RAM-authoritative recovery is the selected
  software strategy; hardware validation and dedicated user-visible warnings are
  the remaining work.

Exit signal:

- Storage changes can be reviewed against a known compatibility and failure
  policy.

### Sprint 5: UI Visual Validation And Render Maintainability

Goal: make UI projection quality visible and keep rendering code readable.

Current status: started. The native SDL app exposes `--capture-bmp`,
`--capture-scenario`, and `--capture-frames`; use `ms build core --target native`
first, then run the generated native binary from the workspace `bin` directory.
See [Sprint 5: UI Visual Validation And Render Maintainability](sprint-5-ui-visual-validation.md).

Why this is a good idea:

- Source-level LVGL lifetime is mapped, but visual correctness was not proven.
- UI code already follows projection patterns, so screenshots can catch layout
  regressions without forcing broad architecture changes.

Evidence:

- `lvgl-ui-lifetime-map.md` maps UI ownership, timers, overlays, and cleanup.
- `domain-ui-rendering.md` identifies view-model builders and StepGrid render
  seams.
- Remaining dark zones include visual overlap, clipping, frame pacing, and
  display tearing.

Risks:

- Screenshot tests may be brittle if not scoped.
- UI extraction can fragment simple render code.

Mitigations:

- Start with screenshots/manual captures for major screens and overlays.
- Only extract render/model code where it reduces local reasoning cost.
- Keep visual checks focused on stable layout contracts.

Exit signal:

- Main screens and overlays have a repeatable visual validation path.

### Realtime Validation: Runtime And Hardware Proof

Goal: validate the runtime assumptions that native tests cannot prove.

Why this is a good idea:

- Native tests cover logic, but not Teensy timer budgets, USB MIDI pressure, SD
  latency, display DMA behavior, or electrical input behavior.
- Realtime MIDI quality depends on missed deadlines, queue depth, drain budgets,
  and display/storage interference.

Evidence:

- Current source shows the Teensy boot, timer, display, storage, and USB MIDI
  paths, but native tests do not prove timing under load.
- `main.cpp` creates `SequencerRuntimeService` and registers the pre-context
  hook.
- `StandaloneContext::update()` is intentionally empty for runtime work.

Risks:

- Hardware validation can become anecdotal if counters are incomplete.
- Running hardware stress too late may uncover architectural issues after other
  work has stacked on top.

Mitigations:

- Keep integration proof narrow: active standalone context plus play state should
  cause playhead progression through the surviving runtime owner.
- Capture PERF_LOG output and MIDI queue/drop/drain counters for hardware runs.
- Run stress with LVGL refresh, sequencer output, MIDI clock, and SD commit
  activity together.

Exit signal:

- Runtime risk discussions can cite measured counters instead of intuition.

## Portfolio Risks

- Treating all exploration notes as equally current.
  Mitigation: each chantier must cite current source files or explicitly mark a
  point as unverified.
- Leaving legacy docs in the visible entry path.
  Mitigation: current contracts live in standardized docs and `.hpp` comments;
  obsolete docs are retired rather than visually mixed with live guidance.
- Starting implementation before documentation truth is cleaned up.
  Mitigation: Sprint 0 comes first.
- Refactoring wide surfaces without tests.
  Mitigation: each sprint defines an exit signal and minimum proof.
- Over-abstracting.
  Mitigation: follow `ARCHITECTURE_REVIEW_RULES.md`: introduce abstractions only
  when duplication is real, stable, and domain meaning remains visible.

## Suggested Review Cadence

For each chantier, before implementation:

1. Re-read the relevant exploration map.
2. Verify current source symbols with `rg`.
3. Mark claims as confirmed, probable, or unverified.
4. Pick the smallest validation artifact: doc cleanup, focused test, screenshot,
   or hardware log.
5. Only then split into implementation tasks.
