# Sprint 1: CoreState Domain Boundaries Analysis

Updated: 2026-04-29

## Scope

This document records the Sprint 1 analysis target and the first implementation
gates used to reduce the `CoreState` access surface without changing ownership.

The architecture chantier index routes Sprint 1 to the `CoreState` /
domain-boundary chantier. Runtime and hardware proof remain required, but they
are tracked as realtime validation instead of the current Sprint 1 scope.

This pass does not propose splitting `CoreState` storage ownership yet. It
classifies existing access, confirms the real invariants, and tracks the
lowest-risk places where implementation can reduce ambiguity.

## Architecture Decision

Decision: do an ambitious progressive reduction of the `CoreState` surface, not
a complete rewrite now.

`CoreState` should remain the private ownership and lifecycle root while
handlers, UI projections, and ordinary domain services migrate away from broad
`CoreState&` / `CoreState*` dependencies. The target shape is:

- composition code may hold `CoreState&` to assemble dependencies;
- runtime stays on narrow `StateRefs`;
- UI projections receive read-model sources, not `CoreState`;
- handlers and domain services receive `StateRefs` plus typed operations;
- cross-domain behavior stays in named workflows/coordinators;
- shared track becomes the first explicit coordinator/invariant boundary.

Why this is the preferred decision:

- The current code already has useful architecture seams: `StateRefs`, domain
  services, lifecycle/bootstrap separation, and runtime isolation.
- The broad access problem is concentrated enough to reduce incrementally:
  `src/handler`, `src/ui`, and selected workflows are the main surfaces.
- Tests already cover several critical invariants, so a staged migration can be
  protected without freezing feature work.
- A rewrite would duplicate existing embedded ownership, persistence, and
  realtime behavior before proving that those parts are fundamentally wrong.
- Keeping physical storage ownership stable is safer for memory footprint,
  allocation behavior, and realtime confidence.

A complete rewrite becomes justified only if one of these gates is hit:

- shared-track coordination cannot be made explicit without preserving broad
  `CoreState` access in handlers/UI;
- macro services cannot be migrated to `StateRefs` plus typed operations without
  worse coupling or noisy pass-through APIs;
- persistence/runtime invariants remain unreadable after shared-track and macro
  service extraction;
- tests cannot characterize the behavior that would be changed by the staged
  refactor;
- measured memory/performance constraints are worse under the staged coordinator
  approach than under a redesigned root.

Current verification did not hit those gates.

## Evidence Base

Source-recognition docs:

- Local notes under `docs/_codex-exploration/`, when present, were used as
  navigation during the original pass. They included `codebase-map.md`,
  `major-discoveries.md`, `domain-*.md`, `remaining-dark-zones.md`, and
  `tests-by-domain.md`.
- `docs/_codex-exploration/` is intentionally excluded from Git, so those notes
  are not a repo contract. The tracked Sprint 1 claims below must be verified
  against current source, current tests, and the commands listed in this file.

Sprint 0 docs:

- [`sprint-0-documentation-source-of-truth.md`](sprint-0-documentation-source-of-truth.md)
  explicitly excluded `CoreState` access refactoring from Sprint 0.
- [`hpp-contract-compliance-map.md`](hpp-contract-compliance-map.md) and
  [`hpp-contract-compliance-tracker.md`](hpp-contract-compliance-tracker.md)
  confirm that headers now document current contracts, including state,
  persistence, runtime, and domain-service boundaries.
- [`sprint-0-handoff.md`](sprint-0-handoff.md) records that Sprint 0 locked the
  documentation baseline, not the access-surface implementation.

Source checks used in this pass:

- `rg -n "CoreState&|CoreState\\*|fromCoreState\\(|StateRefs" src test`
- `rg -n "setSharedTrackState|requestMacroWorkspacePersist|persistSequencerWorkspace|queuePendingSequencer" src test`
- Targeted reads of `CoreState`, macro workflows, sequencer persistence, shared
  track services, standalone assembly, runtime, UI projection, and tests.
- Current native verification entry point: `ms test core`.
- Historical targeted PlatformIO native verification was used during the first
  Sprint 1 pass on 2026-04-28. Keep future verification on the `ms` entry point
  unless a firmware build/upload check is specifically required.
- Implementation verification:
  `git diff --check`, `rg -n "CoreState" src/ui -g "*.hpp" -g "*.cpp"`,
  `rg -n "CoreState\\* state_|explicit Macro.*DomainServices\\(core::state::CoreState|Macro.*DomainServices\\(core::state::CoreState" src/handler/macro -g "*.hpp" -g "*.cpp"`,
  and `ms test core`.

## Confirmed Findings

| Finding | Status | Evidence | Sprint 1 implication |
|---|---|---|---|
| `CoreState` is the standalone state authority, not a temporary convenience object. | Confirmed | `src/state/CoreState.hpp:148-155`, `src/state/CoreState.cpp:100-124`, `main.cpp:187-194` | Do not split ownership first. Start by classifying access intent. |
| `CoreState` exposes many public aliases into durable macro/sequencer domains and shared UI/system state. | Confirmed | `src/state/CoreState.hpp:166-196` | The friction is real: a caller can touch many branches after receiving `CoreState&`. |
| Bootstrap/lifecycle privileged access is intentional and narrow. | Confirmed | `src/state/CoreState.hpp:155-157`, `src/state/CoreStateLifecycle.cpp`, `src/state/CoreStateBootstrap.cpp`, `hpp-contract-compliance-tracker.md` | Treat lifecycle/bootstrap as allowed authority, not cleanup targets. |
| Composition roots legitimately receive `CoreState&` to build modules, `StateRefs`, and domain services. | Confirmed | `src/context/StandaloneContext.hpp:63-67`, `src/context/StandaloneContext.hpp:103`, `src/context/standalone/StandaloneFeatureAssembly.cpp:23-92` | Composition access is acceptable if it only wires narrower dependencies. |
| The runtime already uses a strong narrow-access contract. | Confirmed | `src/sequencer/SequencerRuntimeService.hpp:22-40`, `main.cpp:208-218`, `sprint-0-handoff.md` | Preserve this as a rule: runtime should not grow a `CoreState&` dependency. |
| Feature modules and handlers mostly follow the `StateRefs` plus domain-service pattern. | Confirmed | `src/context/standalone/StandaloneFeatureAssembly.cpp:38-92`, many handler `StateRefs` definitions found by `rg` | Sprint 1 should standardize an existing pattern, not invent one. |
| Cross-domain invariants are currently concentrated in named workflows/services. | Confirmed | `src/state/macro/MacroWorkflow.cpp:60-85`, `src/state/sequencer/SequencerPersistenceWorkflow.cpp:23-43`, `src/state/sequencer/SequencerPersistenceWorkflow.cpp:75-123`, `src/handler/macro/MacroStructureDomainServices.cpp:18-46` | These are legitimate broad operations, but their authority should be explicit. |
| Shared track state is the clearest cross-domain invariant. | Confirmed and extracted | `src/state/shared/SharedTrackCoordinator.hpp`, `src/state/shared/SharedTrackCoordinator.cpp`, `src/state/CoreState.cpp::setSharedTrackState_`, `src/handler/common/SharedTrackDomainServices.cpp` | Keep shared-track synchronization in the coordinator; callers request the invariant instead of writing every branch. |
| Data Manager already has a useful seam: `StateRefs` and typed operations; its `CoreState` bridge belongs in command execution, not the UI workflow. | Confirmed and narrowed | `src/state/DataManagerWorkflow.hpp`, `src/handler/settings/DataManagerDomainServices.cpp`, `src/state/DataManagerCommandExecutor.hpp`, `test/test_DataManagerDomainServices/test_main.cpp` | Keep Data Manager UI flow testable without `CoreState`; persistence dispatch remains a named bridge. |
| UI projection had a broad read candidate. | Confirmed and narrowed | `src/ui/common/GlobalTrackNavigationStripModel.hpp::GlobalTrackNavigationStripSource`, `src/ui/common/TrackNavigationStripProps.hpp`, `src/context/standalone/StandaloneUiAssembly.cpp`, `test/test_GlobalTrackNavigationStripModel/test_main.cpp` | Keep projection builders on focused read sources; composition may still assemble those sources from `CoreState`. |
| Macro domain services can operate without stored `CoreState*`. | Confirmed and narrowed | `src/handler/macro/MacroEditDomainServices.hpp`, `src/handler/macro/MacroPerformanceDomainServices.hpp`, `src/handler/macro/MacroStructureDomainServices.hpp`; static check listed above | Keep `fromCoreState(...)` as a production bridge only; ordinary service methods use `StateRefs` plus typed operations. |
| Tests encode direct `CoreState` usage for state authority and persistence. | Confirmed | `test/test_CoreStateAuthority/test_main.cpp`, `test/test_CoreStateLifecycle/test_main.cpp`, `test/test_CoreStatePersistence/test_main.cpp`, `ms test core` | Do not treat test directness as production design smell. Preserve behavior coverage. |

## Access Surface Classification

| Category | Examples | Assessment | Strategy |
|---|---|---|---|
| State authority | `CoreState`, `CoreStateLifecycle`, `CoreStateBootstrap` | Legitimate | Keep authority localized and documented. |
| Composition root | `main.cpp`, `StandaloneContext`, `StandaloneFeatureAssembly` | Legitimate | Allow `CoreState&` only for construction/wiring into narrower refs/services. |
| Runtime | `SequencerRuntimeService::StateRefs` | Positive boundary | Preserve. Any widening to `CoreState&` should be a design review blocker. |
| Handler/module state refs | feature modules and handlers assembled from `StateRefs` | Positive boundary | Standardize this as the default interaction shape. |
| Domain-service bridge | `MacroEditDomainServices::fromCoreState`, `MacroPerformanceDomainServices::fromCoreState`, `MacroStructureDomainServices::fromCoreState`, `SharedTrackDomainServices::fromCoreState`, `DataManagerDomainServices::fromCoreState` | Justified as production bridges; macro internals are now narrowed | Keep factories at composition roots; ordinary service state access remains explicit through `StateRefs` and typed operations. |
| Cross-domain workflow | `MacroWorkflow`, `SequencerPersistenceWorkflow`, `DataManagerWorkflow` | Justified where it coordinates persistence/status/shared track/deferred apply | Name the invariant being coordinated; avoid generic helpers that hide multi-branch writes. |
| UI projection | `GlobalTrackNavigationStripModel` | Narrowed | Keep the focused projection source as the default shape for future UI read models. |
| Tests/support | direct `CoreState` setup in state and persistence tests | Legitimate | Keep direct setup where the test is validating global authority. |

## Production Access Inventory

Inventory source: `rg -n "CoreState&|CoreState\\*|fromCoreState\\(" src main.cpp`.
This table groups all production hits by role so Sprint 1 can start from a
verified access map instead of a vague "CoreState is broad" claim.

| File / symbol | Category | Current judgement | Evidence |
|---|---|---|---|
| `main.cpp` / `coreState.emplace` and runtime construction | State owner / runtime wiring | Legitimate. `main.cpp` owns the singleton state and passes narrow runtime refs. | `main.cpp:187-218` |
| `src/app/AppLogic.hpp::registerContexts` | Composition | Legitimate factory bridge into `StandaloneContext`. | `src/app/AppLogic.hpp:17-24` |
| `src/context/StandaloneContext.*` | Composition / lifecycle | Legitimate. Context stores external state that survives context switches. | `src/context/StandaloneContext.hpp:63-67`, `src/context/StandaloneContext.hpp:103`, `src/context/StandaloneContext.cpp:26` |
| `src/context/standalone/StandaloneUiAssembly.*` | UI assembly plus projection source | Legitimate composition. It builds `GlobalTrackNavigationStripSource` from `CoreState` and passes a focused source to the model. | `src/context/standalone/StandaloneUiAssembly.cpp`, `src/ui/common/GlobalTrackNavigationStripModel.hpp` |
| `src/context/standalone/StandaloneOverlayAssembly.*` | Overlay composition | Legitimate. Uses `CoreState` to bind overlay manager to shared overlay state. | `src/context/standalone/StandaloneOverlayAssembly.cpp:19-27`, `src/context/standalone/StandaloneOverlayAssembly.cpp:58-67` |
| `src/context/standalone/StandaloneFeatureAssembly.*` | Feature composition | Legitimate. This is the main place where `CoreState` is converted into `StateRefs` and domain services. | `src/context/standalone/StandaloneFeatureAssembly.cpp:23-92` |
| `src/context/standalone/StandaloneGlobalHandlerAssembly.*` | Handler composition | Legitimate if kept as wiring only. It passes narrow refs to transport and view switcher handlers. | `src/context/standalone/StandaloneGlobalHandlerAssembly.cpp:19-59`, `src/context/standalone/StandaloneGlobalHandlerAssembly.cpp:68-85` |
| `src/context/standalone/MacroViewActivationContract.hpp` | Cross-domain activation contract | Legitimate but worth keeping named. It syncs macro runtime and status label on view activation. | `src/context/standalone/MacroViewActivationContract.hpp:8-17` |
| `src/state/CoreState.*` | State authority | Legitimate authority. This is the object under review, not an access smell by itself. | `src/state/CoreState.hpp:148-269`, `src/state/CoreState.cpp:100-355` |
| `src/state/CoreStateBootstrap.*` | Bootstrap authority | Legitimate privileged initialization path. | `src/state/CoreState.hpp:155-157`, `src/state/CoreStateBootstrap.hpp:15-24`, `src/state/CoreStateBootstrap.cpp:65-168` |
| `src/state/CoreStateLifecycle.*` | Lifecycle authority | Legitimate privileged lifecycle path. | `src/state/CoreStateLifecycle.hpp:17-39`, `src/state/CoreStateLifecycle.cpp:14-198` |
| `src/state/macro/MacroWorkflow.*` | Macro cross-domain workflow | Legitimate where it coordinates pages, status label, config revision, shared track, and persistence request. Pure runtime/page projection now uses narrow refs. | `src/state/macro/MacroWorkflow.hpp`, `src/state/macro/MacroWorkflow.cpp` |
| `src/state/macro/MacroPersistenceWorkflow.*` | Persistence workflow | Legitimate persistence boundary. Defer structural changes to persistence sprint unless Sprint 1 only clarifies contracts. | `src/state/macro/MacroPersistenceWorkflow.hpp:21-23`, `src/state/macro/MacroPersistenceWorkflow.cpp:11-41` |
| `src/state/sequencer/SequencerPersistenceWorkflow.*` | Persistence workflow / deferred apply | Legitimate broad workflow because it uses persistence readiness, playing state, pending apply, track bank snapshots, shared track state, and workspace persistence. | `src/state/sequencer/SequencerPersistenceWorkflow.hpp:21-31`, `src/state/sequencer/SequencerPersistenceWorkflow.cpp:12-138` |
| `src/state/DataManagerWorkflow.*` | Workflow facade | Positive pattern, now narrowed. It exposes `StateRefs` plus typed operations and has no `CoreState` overloads. | `src/state/DataManagerWorkflow.hpp`, `src/state/DataManagerWorkflow.cpp` |
| `src/state/DataManagerCommandExecutor.*` | Persistence command bridge | Legitimate but broad. It dispatches Data Manager commands to macro/sequencer persistence workflows. | `src/state/DataManagerCommandExecutor.hpp:21-22`, `src/state/DataManagerCommandExecutor.cpp:25-140` |
| `src/state/DataManagerShortcutPersistence.*` | Settings shortcut bridge | Narrowed. It persists shortcut refs through `ShortcutStateRefs`; `CoreState` overloads were removed. | `src/state/DataManagerShortcutPersistence.hpp`, `src/state/DataManagerShortcutPersistence.cpp` |
| `src/handler/settings/DataManagerDomainServices.*` | Domain-service bridge | Positive pattern. Factory captures narrow refs plus hook context. | `src/handler/settings/DataManagerDomainServices.hpp:25`, `src/handler/settings/DataManagerDomainServices.cpp:9-23` |
| `src/handler/common/SharedTrackDomainServices.*` | Shared-track service bridge | Legitimate. It now delegates the cross-domain mutation to `CoreState::setSharedTrackState`, which delegates synchronization to `SharedTrackCoordinator`. | `src/handler/common/SharedTrackDomainServices.hpp`, `src/handler/common/SharedTrackDomainServices.cpp`, `src/state/shared/SharedTrackCoordinator.hpp` |
| `src/handler/macro/MacroEditDomainServices.*` | Macro domain-service bridge | Legitimate and narrowed. It stores macro page refs plus typed operations; `fromCoreState(...)` is the composition bridge. | `src/handler/macro/MacroEditDomainServices.hpp`, `src/handler/macro/MacroEditDomainServices.cpp` |
| `src/handler/macro/MacroPerformanceDomainServices.*` | Macro domain-service bridge | Legitimate and narrowed. Runtime/status/config refs are explicit; persistence/config/page workflows are typed operations. | `src/handler/macro/MacroPerformanceDomainServices.hpp`, `src/handler/macro/MacroPerformanceDomainServices.cpp` |
| `src/handler/macro/MacroStructureDomainServices.*` | Macro structure orchestration | Legitimate and narrowed. It still coordinates page/track structure, presentation refresh, persistence, and shared-track requests, but no longer stores `CoreState*`. | `src/handler/macro/MacroStructureDomainServices.hpp`, `src/handler/macro/MacroStructureDomainServices.cpp` |
| `src/ui/common/GlobalTrackNavigationStripModel.*` | UI projection | Narrowed. It reads `GlobalTrackNavigationStripSource` and emits renderer props without `CoreState`. | `src/ui/common/GlobalTrackNavigationStripModel.hpp`, `src/ui/common/GlobalTrackNavigationStripModel.cpp`, `src/ui/common/TrackNavigationStripProps.hpp` |
| `src/sequencer/SequencerRuntimeService.hpp` | Runtime invariant | No production `CoreState&` dependency; the header explicitly forbids widening unless the runtime contract changes. | `src/sequencer/SequencerRuntimeService.hpp:22-40` |

## Architectural Risks

1. Broad mutable access remains hard to reason about.
   Evidence: `CoreState` aliases at `src/state/CoreState.hpp:166-196` expose
   macro, sequencer, UI, settings, and persistence domains through one object.
   Risk: new contributors may mutate unrelated branches from any service that
   receives `CoreState&`.
   Mitigation: first produce an access inventory and label each access as
   authority, composition, workflow, domain-service bridge, UI projection, or
   test-only.

2. Shared track is a cross-domain invariant with multiple entry points.
   Evidence: `CoreState::setSharedTrackState_` synchronizes shared UI state,
   macro pages, sequencer track bank, and active sequencer track at
   `src/state/CoreState.cpp:308-318`; callers include macro structure services,
   macro workflow, sequencer persistence, and shared track services.
   Risk: a future caller can bypass part of the invariant if it writes directly
   to `pages`, `sequencerTracks`, `sharedTrackActive`, or `sharedTrackEnabledMask`.
   Mitigation: make shared-track mutation rules the first concrete Sprint 1
   boundary contract.

3. Domain services can be both clean facades and broad mutation bridges.
   Evidence: `MacroStructureDomainServices` owns structure commands but also
   flushes persistence, mutates pages, updates shared track, updates status
   presentation, bumps config revision, and requests workspace persistence in
   `src/handler/macro/MacroStructureDomainServices.cpp:18-52` and
   `src/handler/macro/MacroStructureDomainServices.cpp:85-271`.
   Risk: the label "domain service" can hide whether a service is pure domain
   behavior, persistence coordination, UI presentation sync, or cross-domain
   orchestration.
   Mitigation: classify service methods by responsibility before changing them.

4. Workflow overloads can still mix narrow and broad shapes if unchecked.
   Evidence: `MacroWorkflow` now keeps broad `CoreState&` methods only for
   page/track/config workflows that touch persistence, status, config revision,
   or shared track; pure projection uses narrow macro refs.
   Risk: future code may reintroduce broad convenience overloads.
   Mitigation: keep the HPP rule explicit and remove forwarding-only overloads
   in the same change that makes them unnecessary.

5. Sprint routing can drift if the index is not kept current.
   Evidence: `docs/architecture-chantiers/README.md` is the visible routing table
   for architecture chantiers.
   Risk: contributors may plan against stale sprint ownership.
   Mitigation: update the chantier index in the same change that changes sprint
   scope.

## Reliability And Runtime Risks

1. Shared-track persistence is debounced and side-effectful.
   Evidence: `CoreState::requestSharedTrackPersist_` and
   `CoreState::persistSharedTrackState_` at `src/state/CoreState.cpp:264-289`;
   `setSharedTrackState_` requests persistence only when the sanitized state
   changes at `src/state/CoreState.cpp:308-318`.
   Risk: refactoring entry points without preserving change detection could
   create unnecessary writes or missed persistence.
   Mitigation: keep tests around "no change returns false", sanitized active
   track, and persistence request behavior before changing this path.

2. Sequencer load behavior depends on playback state.
   Evidence: `SequencerPersistenceWorkflow::loadPatternSlot` and `loadSetSlot`
   queue pending apply while playing and apply directly when stopped
   (`src/state/sequencer/SequencerPersistenceWorkflow.cpp:23-43`,
   `src/state/sequencer/SequencerPersistenceWorkflow.cpp:75-123`).
   Risk: narrowing access too aggressively can accidentally separate status,
   pending apply, sequencer state, and track bank decisions.
   Mitigation: treat persistence workflows as cross-domain workflows until a
   dedicated persistence sprint revisits them.

3. Runtime access must stay narrow.
   Evidence: `SequencerRuntimeService` explicitly states "avoid widening this to
   CoreState&" in `src/sequencer/SequencerRuntimeService.hpp:29-31`.
   Risk: using runtime as a shortcut for shared state would reintroduce a second
   broad state access path inside realtime code.
   Mitigation: make runtime `CoreState&` absence a Sprint 1 invariant.

## Performance Risks

1. Access-surface refactors can create extra signal churn.
   Evidence: shared track writes fan out to signals and track switching in
   `src/state/shared/SharedTrackCoordinator.cpp:37-68`.
   Risk: decomposing the operation into multiple smaller calls could emit
   duplicate signals or switch tracks twice.
   Mitigation: keep shared-track mutation atomic at the public boundary.

2. Persistence and SD writes should not be accidentally made more eager.
   Evidence: macro structure services call `flushAutoPersist`,
   `requestMacroWorkspacePersist`, and shared-track persistence paths
   (`src/handler/macro/MacroStructureDomainServices.cpp:24-52`,
   `src/state/CoreState.cpp:233-289`).
   Risk: moving code without preserving debounce boundaries can increase write
   pressure.
   Mitigation: treat persistence behavior as observable runtime behavior, even
   if the Sprint 1 focus is architecture.

## Maintainability Risks

1. The codebase has two good patterns that need clearer selection rules:
   narrow `StateRefs` and broad domain workflows. Evidence:
   `StandaloneFeatureAssembly` wires both shapes at
   `src/context/standalone/StandaloneFeatureAssembly.cpp:38-92`.
   Mitigation: define a short decision table before implementation.

2. UI projection can look like domain authority if it reads `CoreState`
   directly. Evidence: `GlobalTrackNavigationStripModel` reads track selection,
   structure focus, shared track, and status activity from `CoreState` at
   `src/ui/common/GlobalTrackNavigationStripModel.cpp:15-50`.
   Mitigation: make projection-only dependencies narrower where low-risk.

3. Tests are numerous but not a complete access-contract proof. Evidence:
   `ms test core` covers the current native suite, while the remaining
   validation gaps are still full input semantics, hardware timing, visual
   correctness, and full callback graph.
   Mitigation: use existing tests as guardrails, then add targeted tests only
   where Sprint 1 changes a boundary.

## Probable Duplications Or Near-Duplications

These started as inventory candidates; the items below now reflect the current
implementation status.

- Multiple code paths request the same macro post-mutation effects: flush or
  persist workspace, update active page presentation, sync runtime, and bump
  config revision. Evidence: `MacroWorkflow::switchToPage`,
  `MacroWorkflow::switchToTrack`, and `MacroStructureDomainServices` helper
  functions.
- Shared-track mutations now converge through `SharedTrackCoordinator`, but
  callers still compute desired masks/active tracks before requesting the
  invariant. Evidence: `SharedTrackCoordinator`, `MacroStructureDomainServices`,
  `SequencerPersistenceWorkflow`, and `SharedTrackDomainServices`.
- Data Manager and macro services now use the same broad shape: focused state
  refs plus typed operations, with `fromCoreState(...)` as the production bridge
  rather than the normal service API.

## Legacy Debt Not Proven

Do not claim these as bugs without more evidence:

- Public aliases in `CoreState` are current integration API until each caller
  has a narrower contract.
- `fromCoreState(...)` factories are not inherently wrong; composition roots use
  them to wire services.
- Direct `CoreState` usage in tests is not legacy pollution when tests validate
  global authority.
- Broad workflow methods are not automatically design debt when they preserve a
  cross-domain invariant.
- No code should be declared dead from this sprint alone. Dead-code
  classification requires caller search, build-target search, tests or compile
  checks, git history, and platform-guard inspection.

## Recommended Sprint 1 Strategy

P0:

- Keep the architecture chantier index aligned so "Sprint 1" points to the
  current `CoreState` / domain-boundary scope.
- Use the production access inventory above as the Sprint 1 planning source, and
  refresh it before implementation if the branch moves.
- Keep every usage assigned to one category: state authority, composition,
  runtime, domain-service bridge, cross-domain workflow, UI projection, or
  test/support.
- Lock three explicit rules:
  1. runtime stays `StateRefs`, not `CoreState&`;
  2. composition roots may receive `CoreState&` only to wire narrower contracts;
  3. cross-domain writes must live behind named workflows/services.

P1:

- Define the shared-track boundary contract first, because it synchronizes macro
  pages, sequencer track bank, active sequencer track, shared UI signals, and
  settings persistence.
- Keep the narrowed `GlobalTrackNavigationStripModel` projection source as the
  model for future UI read-model builders.
- Keep macro and Data Manager service responsibilities on focused `StateRefs`
  plus typed operations; avoid reintroducing stored `CoreState*` in handlers.

P2:

- Defer persistence workflow restructuring to the persistence sprint unless
  Sprint 1 needs a small contract comment or test.
- Defer hardware timing, visual correctness, input state-machine conflicts, and
  full callback graph validation to their dedicated chantiers.
- Defer removal of `CoreState` public aliases until the inventory proves a real
  simplification path.

## Progressive Refactor Gates

Each implementation step should be small enough to answer one question:
"did this reduce broad `CoreState` access while preserving behavior?" A step is
not complete until both the code and the old access path have been cleaned.

Baseline before the first code edit:

- Refresh the production inventory:
  `rg -n "CoreState&|CoreState\\*|fromCoreState\\(" src main.cpp -g "*.hpp" -g "*.cpp"`.
- Save the current counts by category: composition, state authority, workflow,
  domain-service bridge, UI projection.
- Run the native unit-test baseline:
  `ms test core`.
- Identify the exact behavior protected by the step before changing signatures.

Gate 1: sprint index and access policy.

- Status: complete.
- Keep the architecture chantier index aligned with the current Sprint 1 scope.
- No production behavior change.
- Check: docs link check by path and `rg -n "Sprint 1|Sprint 2|CoreState Access" docs/architecture-chantiers`.

Gate 2: low-risk UI projection narrowing.

- Status: complete for `GlobalTrackNavigationStripModel`.
- Replace `GlobalTrackNavigationStripModel`'s broad `const CoreState&` input with
  a focused projection source.
- Keep generated props byte-for-byte equivalent for existing scenarios.
- Required tests: add or update a focused projection test if no current test
  fails on a wrong projection; otherwise run the nearest UI/model test plus the
  critical native baseline.
- Cleanup rule: remove the old `CoreState&` overload unless a real caller still
  needs it. If a temporary overload is unavoidable, it must live at composition
  level and be removed by the next gate.
- Check: `rg -n "CoreState" src/ui src/handler -g "*.hpp" -g "*.cpp"` must show
  one fewer UI dependency or a documented reason why the count is unchanged.

Gate 3: shared-track authority extraction.

- Status: complete for the central macro/sequencer/UI shared-track invariant.
- Introduce or formalize a single shared-track authority boundary that owns mask
  sanitization, active-track sanitization, macro page sync, sequencer track-bank
  sync, active sequencer switching, UI signals, and settings persistence request.
- Required tests: `test_SharedTrackDomainServices`,
  `test_CoreStateLifecycle`, `test_CoreStatePersistence`,
  `test_MacroPerformanceHandler`, and `test_SequencerStepHandler` when handler
  call sites change.
- Cleanup rule: direct writes to shared-track related state outside the authority
  are removed or converted in the same step. Do not leave parallel "old" and
  "new" shared-track mutation paths.
- Check: `rg -n "sharedTrackActive|sharedTrackEnabledMask|setSharedTrackState|syncSharedTrackState" src test`
  should show all production mutations routing through the authority or through
  narrowly documented state internals.

Gate 4: macro domain services migration.

- Status: complete for `MacroEditDomainServices`,
  `MacroPerformanceDomainServices`, and `MacroStructureDomainServices`.
- Convert macro services away from stored `CoreState*` toward `StateRefs` plus
  typed `Operations`, starting with the smallest service that proves the pattern.
- Required tests: `test_MacroPerformanceDomainServices`,
  `test_MacroPerformanceHandler`, `test_MacroEditHandler`, and any new test for
  `MacroStructureDomainServices` if structure services change.
- Cleanup rule: after each service migration, remove the unused `CoreState`
  constructor/member from that service. Do not keep compatibility constructors
  in handler-owned headers once composition has moved.
- Check: `rg -n "CoreState\\* state_|fromCoreState|CoreState&" src/handler/macro -g "*.hpp" -g "*.cpp"`
  must trend down after each macro service step.

Gate 5: workflow boundary review.

- Status: complete for macro and Data Manager workflows. Persistence workflows
  remain broad by design because they coordinate storage readiness, save/load,
  deferred sequencer apply, shared-track refresh, and workspace persistence.
- Keep broad workflow methods only when they coordinate a named invariant:
  persistence, shared track, deferred sequencer apply, status projection, config
  revision, or lifecycle.
- Required tests: run the workflow's domain tests plus the critical native
  baseline.
- Cleanup rule: if a broad overload only forwards to a narrow overload and all
  callers can use the narrow form, remove it in the same step.
- Check: public HPP contracts explain why any remaining broad workflow method
  needs broad access.

Gate 6: alias and include cleanup.

- Status: complete for cleanup introduced or unlocked by Gates 2-5. Remaining
  public `CoreState` aliases are current integration API until their callers
  gain narrower contracts.
- Only after call sites move, reduce public `CoreState` aliases or includes that
  no longer serve a production caller.
- Required tests: full native suite if public state headers change broadly;
  otherwise run affected domain tests plus baseline.
- Cleanup rule: remove dead includes, stale forward declarations, unused helper
  functions, and obsolete tests/helpers in the same change that makes them
  unreachable.
- Check: `rg -n "CoreState" src/handler src/ui -g "*.hpp" -g "*.cpp"` should be
  empty or limited to composition-approved bridges by the end of Sprint 1.

Quality bar for every gate:

- No new `CoreState&` / `CoreState*` dependency outside state authority,
  composition, or explicitly justified workflows.
- No new generic "manager" helper hiding multi-domain writes.
- No duplicate old/new mutation path after the gate closes.
- Public HPP docs explain why a boundary exists only when the reason is not
  obvious from the type shape. CPP comments stay minimal.
- Tests are added only where behavior would otherwise be unprotected.
- `git diff` must show a net reduction in ambiguity, not just renamed access.

## Current Implementation Status

Completed in the first implementation tranche:

- `docs/architecture-chantiers/README.md` now routes Sprint 1 to `CoreState`
  surface reduction and keeps realtime proof as a separate validation track.
- `src/ui/common/GlobalTrackNavigationStripModel.*` now receives
  `GlobalTrackNavigationStripSource`; `TrackNavigationStripProps` lives in its
  own dependency-light header.
- `src/state/shared/SharedTrackCoordinator.*` centralizes shared-track
  sanitization and synchronization across macro pages, sequencer track bank,
  active sequencer editor, and shared UI signals.
- `src/handler/macro/MacroEditDomainServices.*`,
  `src/handler/macro/MacroPerformanceDomainServices.*`, and
  `src/handler/macro/MacroStructureDomainServices.*` no longer store
  `CoreState*`; their production bridges are `fromCoreState(...)`.
- `src/state/macro/MacroWorkflow.*` no longer exposes forwarding-only
  `CoreState` overloads for pure macro state projection.
- `src/state/DataManagerWorkflow.*` and
  `src/state/DataManagerShortcutPersistence.*` no longer expose `CoreState`
  overloads; `src/state/DataManagerCommandExecutor.*` remains the named
  persistence bridge.
- New focused tests cover the UI projection and shared-track coordinator:
  `test/test_GlobalTrackNavigationStripModel/test_main.cpp` and
  `test/test_SharedTrackCoordinator/test_main.cpp`.
- Full native verification passed after these changes through the workspace
  entry point: `ms test core`.

Remaining Sprint 1 follow-up boundary:

- Keep composition-approved `fromCoreState(...)` bridges unless a composition
  root can wire the narrower refs directly without increasing noise.
- Reduce remaining public `CoreState` aliases only when a concrete caller moves
  to a narrower contract in the same change.

## Candidate Files For Sprint 1

Primary inspection targets:

- `src/state/CoreState.hpp`
- `src/state/CoreState.cpp`
- `src/state/CoreStateLifecycle.hpp`
- `src/state/CoreStateBootstrap.hpp`
- `src/state/macro/MacroWorkflow.hpp`
- `src/state/macro/MacroWorkflow.cpp`
- `src/state/sequencer/SequencerPersistenceWorkflow.hpp`
- `src/state/sequencer/SequencerPersistenceWorkflow.cpp`
- `src/state/DataManagerWorkflow.hpp`
- `src/state/DataManagerWorkflow.cpp`
- `src/handler/common/SharedTrackDomainServices.hpp`
- `src/handler/common/SharedTrackDomainServices.cpp`
- `src/handler/macro/MacroPerformanceDomainServices.hpp`
- `src/handler/macro/MacroPerformanceDomainServices.cpp`
- `src/handler/macro/MacroStructureDomainServices.hpp`
- `src/handler/macro/MacroStructureDomainServices.cpp`
- `src/handler/settings/DataManagerDomainServices.hpp`
- `src/handler/settings/DataManagerDomainServices.cpp`
- `src/context/StandaloneContext.hpp`
- `src/context/standalone/StandaloneFeatureAssembly.cpp`
- `src/ui/common/GlobalTrackNavigationStripModel.hpp`
- `src/ui/common/GlobalTrackNavigationStripModel.cpp`
- `main.cpp`

Implemented first candidates after inventory:

- `docs/architecture-chantiers/README.md`, to reconcile sprint order.
- `src/ui/common/GlobalTrackNavigationStripModel.hpp/.cpp`, now narrowed to a
  focused projection source.
- `src/state/shared/SharedTrackCoordinator.hpp/.cpp`, now the shared-track
  invariant boundary.
- Macro domain services under `src/handler/macro`, now narrowed to `StateRefs`
  plus typed operations.

## Test Strategy

Existing guardrails:

- `test/test_CoreStateAuthority/test_main.cpp`
- `test/test_CoreStateLifecycle/test_main.cpp`
- `test/test_CoreStatePersistence/test_main.cpp`
- `test/test_DataManagerDomainServices/test_main.cpp`
- `test/test_MacroPerformanceDomainServices/test_main.cpp`
- `test/test_MacroPerformanceHandler/test_main.cpp`
- `test/test_MacroViewActivationContract/test_main.cpp`
- `test/test_GlobalTrackNavigationStripModel/test_main.cpp`
- `test/test_SharedTrackCoordinator/test_main.cpp`
- `test/test_RealtimeMidiQueue/test_main.cpp`
- `test/test_SequencerMidiEventSink/test_main.cpp`

Targeted behavior now covered by implementation tests:

- shared-track no-op mutation returns false and does not request persistence;
- shared-track sanitization keeps macro pages and sequencer track bank aligned;
- global track navigation projection produces identical props after narrowing;
- domain-service refactor preserves macro structure post-mutation effects:
  runtime sync, page name, config revision, and persistence request.

## Zones Not Covered

- No hardware timing proof on Teensy.
- No SDL/LVGL visual capture.
- No full modal input state-machine matrix.
- No full callback graph through framework helpers.
- No binary compatibility audit for older persistence formats.
- No dead-code or legacy classification beyond the specific access-surface
  findings listed above.

## Exit Signal

The first Sprint 1 implementation tranche is complete when:

- the sprint index is aligned;
- the production `CoreState&`, `CoreState*`, and `fromCoreState(...)` inventory
  is refreshed against the implementation branch;
- shared-track authority is explicitly documented;
- low-risk narrowing candidates have either been converted or explicitly
  deferred;
- tests needed to protect changed boundaries pass in the full native suite;
- no old/new duplicate mutation path remains for the gates closed so far.

Current status: complete for Gates 1-6 on the scoped implementation tranche as
of 2026-04-29. Follow-up work belongs to Sprint 2+ unless it corrects
documentation drift in the tracked Sprint 1 record.
