# Architecture Chantiers

Purpose: list codebase-scale architecture chantiers that would reduce onboarding
friction, remove ambiguity, and improve maintainability.

This is not an implementation plan. Each chantier below names why it is useful,
what is already confirmed, what still needs verification, and the main risks and
mitigations before implementation work starts.

## Evidence Baseline

Primary exploration sources:

- [`docs/_codex-exploration/README.md`](../_codex-exploration/README.md)
- [`docs/_codex-exploration/codebase-map.md`](../_codex-exploration/codebase-map.md)
- [`docs/_codex-exploration/analysis-readiness.md`](../_codex-exploration/analysis-readiness.md)
- [`docs/_codex-exploration/remaining-dark-zones.md`](../_codex-exploration/remaining-dark-zones.md)
- [`docs/_codex-exploration/input-binding-map.md`](../_codex-exploration/input-binding-map.md)
- [`docs/_codex-exploration/midi-event-flow-map.md`](../_codex-exploration/midi-event-flow-map.md)
- [`docs/_codex-exploration/lvgl-ui-lifetime-map.md`](../_codex-exploration/lvgl-ui-lifetime-map.md)
- [`docs/_codex-exploration/hardware-target-map.md`](../_codex-exploration/hardware-target-map.md)
- [`docs/_codex-exploration/external-dependencies-map.md`](../_codex-exploration/external-dependencies-map.md)

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
  Evidence: the domain maps under `docs/_codex-exploration/domain-*.md`.
- There is a meaningful native test surface by domain.
  Evidence: `docs/_codex-exploration/tests-by-domain.md` and
  `docs/_codex-exploration/analysis-readiness.md`.
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
runtime proof, state-surface review, UI validation, and hardware validation in a
single change wave.

| Todo | Sprint | Why it belongs there |
|---|---|---|
| Finalize and validate this architecture-chantier portfolio | Sprint 0 | It defines the source-of-truth entry point for the rest of the work. |
| Audit active docs for dead links, obsolete seams, and historical/current-contract confusion | Sprint 0 | Documentation truth must be cleaned before using docs to steer implementation. |
| Update docs index/results and retire misleading docs | Sprint 0 | Onboarding depends on an accurate entry path. |
| Verify or add the standalone runtime playhead progression proof | Sprint 1 | This is runtime behavior, not documentation cleanup. |
| Inventory and classify all `CoreState&` / `fromCoreState(...)` usages | Sprint 2 | This is a state-dependency surface review. |
| Build the input/overlay binding state-machine matrix | Sprint 3 | This turns lexical input maps into semantic interaction contracts. |
| Add targeted tests for critical input conflicts | Sprint 3 | Tests should follow the semantic matrix and protect intended behavior. |
| Capture Teensy PERF_LOG runtime stress data | Sprint 4 | Native tests cannot prove hardware timing. |
| Capture queue/drain/drop/jitter metrics | Sprint 4 | These are realtime and hardware validation signals. |
| Capture SDL/LVGL screens and overlays | Sprint 5 | This validates visual UI behavior. |
| Produce a minimal UI visual reference report | Sprint 5 | This makes UI regressions reviewable. |
| Inventory persistence formats and compatibility needs | Sprint 6 | This belongs to storage and migration policy. |
| Decide SD failure/hot-swap policy | Sprint 6 | This is persistence/hardware-storage semantics. |
| Audit only external dependencies traversed by `core` | Sprint 7 | This is targeted dependency validation after local contracts are clearer. |
| Convert validated discoveries into implementation tickets | Cross-sprint closure | Tickets should be produced continuously after each sprint, then consolidated. |

Detailed sprint plans:

- [Sprint 0: Documentation Source Of Truth](sprint-0-documentation-source-of-truth.md)
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

### Sprint 1: Standalone Runtime Contract And Integration Proof

Goal: make the standalone runtime ownership and update path impossible to
misread or regress.

Why this is a good idea:

- Runtime ownership is safety-critical: duplicate runtime instances or duplicate
  MIDI subscriptions can produce subtle side effects.
- A gate test protects the decision logic, but an integration proof is needed to
  catch "the runtime exists but is not actually ticked" failures.

Evidence:

- `main.cpp` creates `SequencerRuntimeService` and registers the pre-context
  hook.
- `StandaloneContext::update()` is intentionally empty for runtime work.
- `StandaloneContext.hpp`, `StandaloneSequencerRuntimeGate.hpp`, and
  `SequencerRuntimeService.hpp` document the current ownership rule at the
  source boundary.
- The exploration maps identify standalone runtime validation as a high-value
  follow-up.

Risks:

- Building a broad integration harness too early could become brittle.
- Test-only seams could accidentally reintroduce a second runtime path.

Mitigations:

- Keep the first test narrow: prove active standalone context plus play state
  causes playhead progression through the surviving runtime owner.
- Avoid adding new production abstractions unless the test cannot be written
  against existing seams.
- Search for all `SequencerRuntimeService` construction sites before declaring
  the contract locked.

Exit signal:

- There is exactly one live standalone runtime owner and at least one test would
  fail if that runtime stopped being updated.

### Sprint 2: `CoreState` Access Surface Review

Goal: preserve `CoreState` as the authority while reducing "grab-bag" access.

Why this is a good idea:

- A central authority is useful, but wide mutable access makes local reasoning
  harder.
- The codebase already has a positive pattern: many handlers/modules consume
  `StateRefs` or domain services. The sprint should standardize when each shape
  is appropriate.

Evidence:

- Include maps show `src/state` is the highest internal include target.
- Existing code has many `StateRefs` seams, but also remaining `CoreState&`
  workflows and `fromCoreState(...)` factories.
- `ARCHITECTURE_REVIEW_RULES.md` warns against helpers that write across
  unrelated `CoreState` branches.

Risks:

- Over-narrowing too soon can create noisy plumbing.
- Splitting state access without understanding invariants can hide cross-domain
  transitions instead of clarifying them.

Mitigations:

- First classify current usages: composition root, state lifecycle, legitimate
  cross-domain workflow, handler/service convenience, UI projection.
- Change only the categories that create real ambiguity.
- Keep cross-domain invariants named and reviewable rather than hidden behind
  generic helpers.

Exit signal:

- New contributors can tell which module owns a mutation without inspecting a
  broad `CoreState` object graph.

### Sprint 3: Input And Overlay State-Machine Map

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

### Sprint 4: Realtime And Hardware Validation

Goal: validate the runtime assumptions that native tests cannot prove.

Why this is a good idea:

- Native tests cover logic, but not Teensy timer budgets, USB MIDI pressure, SD
  latency, display DMA behavior, or electrical input behavior.
- Realtime MIDI quality depends on missed deadlines, queue depth, drain budgets,
  and display/storage interference.

Evidence:

- `hardware-target-map.md` lists the Teensy boot, timer, display, storage, and
  USB MIDI paths.
- `remaining-dark-zones.md` names hardware timing under load as unknown.
- Realtime docs already describe counters, deadlines, and queue/drain concerns.

Risks:

- Hardware validation can become anecdotal if counters are incomplete.
- Running hardware stress too late may uncover architectural issues after other
  work has stacked on top.

Mitigations:

- Define a small repeatable smoke/stress profile before changing behavior.
- Capture PERF_LOG output and MIDI queue/drop/drain counters.
- Run stress with LVGL refresh, sequencer output, MIDI clock, and SD commit
  activity together.

Exit signal:

- Runtime risk discussions can cite measured counters instead of intuition.

### Sprint 5: UI Visual Validation And Render Maintainability

Goal: make UI projection quality visible and keep rendering code readable.

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

### Sprint 6: Persistence Compatibility And Failure Semantics

Goal: clarify storage format compatibility and failure behavior.

Why this is a good idea:

- Current persistence tests prove present-day roundtrips and corruption handling,
  but not necessarily compatibility with older firmware data.
- SD-card failure and hot-swap behavior are not proven by native tests.

Evidence:

- `domain-persistence.md` names missing binary compatibility audit and physical
  SD failure behavior.
- `hardware-target-map.md` documents six storage domains and SD backend behavior.
- `SDCardBackend` has a `reopen()` method, but no main-loop recovery path was
  confirmed in this pass.

Risks:

- Format migration can become invasive if mixed with feature work.
- Over-engineering failure recovery can add complexity beyond current product
  needs.

Mitigations:

- First inventory payload versions and stored domains.
- Add fixtures for any released formats before changing codecs.
- Keep SD recovery policy explicit: unsupported, manual reboot, automatic
  reopen, or user-visible warning.

Exit signal:

- Storage changes can be reviewed against a known compatibility and failure
  policy.

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
