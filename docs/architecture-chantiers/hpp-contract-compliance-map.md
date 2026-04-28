# HPP Contract Compliance Map

Purpose: map where source-level `.hpp` documentation is missing, thin, or
already carrying useful architecture contracts.

This is a cartography document, not the implementation patch. The goal is to
decide which headers deserve standardized contract comments before adding more
runtime, state, UI, or persistence work.

## Method

Evidence sources:

- `docs/_codex-exploration/codebase-map.md`
- `docs/_codex-exploration/analysis-readiness.md`
- `docs/_codex-exploration/remaining-dark-zones.md`
- `docs/_codex-exploration/domain-sequencer-runtime.md`
- `docs/_codex-exploration/domain-macro.md`
- `docs/_codex-exploration/domain-sequencer-editing.md`
- `docs/_codex-exploration/domain-settings-data-manager.md`
- `docs/_codex-exploration/domain-persistence.md`
- `docs/_codex-exploration/domain-ui-rendering.md`
- `docs/_codex-exploration/input-binding-map.md`
- `docs/_codex-exploration/lvgl-ui-lifetime-map.md`

Source checks used to confirm the map:

```powershell
rg --files src -g "*.hpp"
rg -n "/\*\*|///|@brief|ownership|owns|must not|belongs|contract|lifecycle|invariant|StateRefs" src -g "*.hpp"
rg -n "class SequencerRuntimeService|class SequencerInternalTimerLane|class RealtimeMidiQueue|class MidiClockSyncService" src/sequencer -g "*.hpp"
rg -n "struct CoreState|struct CoreStateLifecycle|class PersistenceSlotFileStore|class MacroPersistence|class SequencerPersistence|struct DataManagerWorkflow" src/state src/persistence -g "*.hpp"
rg -n "class StandaloneUiAssembly|class StandaloneFeatureAssembly|class StandaloneOverlayAssembly|class StandaloneGlobalHandlerAssembly" src/context/standalone -g "*.hpp"
```

Classification is heuristic and then source-checked for high-risk seams:

- `contract-ish`: a header already carries some ownership, lifecycle, invariant,
  persistence, runtime, scope, or state-access rationale.
- `brief-only`: a header has descriptive comments but not enough "why" to guide
  architectural edits.
- `missing`: no useful header-level documentation was detected.
- `exempt-candidate`: generated assets or trivial value catalogs that may not
  need a contract comment.

## Initial Coverage Snapshot

Mechanical inventory before the compliance pass: `172` headers under `src`.

| Area | Contract-ish | Brief-only | Missing | Notes |
|---|---:|---:|---:|---|
| `src/sequencer` runtime | 2 | 0 | 13 | Highest risk: realtime and MIDI behavior. |
| `src/persistence` + settings persistence | 1 | 0 | 10 | Binary/layout and storage semantics need contracts. |
| `src/state` excluding persistence | 7 | 0 | 21 | State authority exists, but many workflow/state seams are undocumented. |
| `src/context` composition | 3 | 1 | 16 | Assembly ownership is central to onboarding and lifecycle safety. |
| `src/handler` workflows/input | 9 | 1 | 17 | Input maps show many modal predicates; contracts should explain mode ownership. |
| `src/ui` rendering | 5 | 11 | 27 | Projection pattern exists; render seams need boundary comments. |
| hardware/config | 0 | 7 | 2 | Mostly constants; some platform contracts may be useful. |
| app/api/midi/config misc | 2 | 4 | 6 | Lower priority unless public API or ownership boundary. |
| generated font assets | 0 | 0 | 13 | Exempt candidates. |

Important nuance: `contract-ish` does not mean done. It means a useful rationale
exists somewhere in the header; it may still need cleanup, standard wording, or
legacy removal.

## Confirmed Good Direction

The Sprint 0 cleanup moved the most urgent runtime/lifecycle rationale into
headers:

- `src/sequencer/SequencerRuntimeService.hpp:25` states the single runtime owner
  and warns against a second runtime execution path.
- `src/context/standalone/StandaloneSequencerRuntimeGate.hpp:8` states the pure
  pre-context hook decision rule.
- `src/context/standalone/ActiveViewLifecyclePlan.hpp:13` states that view
  activation side effects belong to the context lifecycle seam.
- `src/context/StandaloneContext.hpp:20` already states that the context stays
  focused on lifecycle and assembly order while the sequencer runtime is owned
  outside the context.

These are good examples of the target style: short, contract-oriented, and close
to the API that future contributors will edit.

## Resolved During Compliance Pass

### CoreState Legacy Wording Removed

`src/state/CoreState.hpp` now describes the current standalone state authority
directly: application-level ownership, durable macro/sequencer domains, shared
UI state, settings storage, and context-safe lifetime.

Why this matters:

- `CoreState` is confirmed by `codebase-map.md` and `domain-*` maps as the
  central standalone state authority.
- Removing the older-context comparison avoids legacy vocabulary in the header
  new contributors are likely to read first.

Applied change:

- Replaced the legacy wording with current standalone ownership language.
- Added compact contracts for macro domain, sequencer domain, UI/system domain,
  lifecycle, bootstrap, state workflows, and persistence formats.

### Useful Contract Exists But Is Not Standardized

`src/context/standalone/MacroViewActivationContract.hpp:9` has a useful local
comment:

- macro-view activation owns runtime/status resync in the standalone lifecycle
  seam.

Why this matters:

- `domain-macro.md` identifies this header as the macro activation sync seam.
- `lvgl-ui-lifetime-map.md` identifies active-view lifecycle as a key UI state
  transition.

Desired change:

- Convert the one-line `//` into a short header-level contract block.

## P0 Contract Headers

P0 headers are high-risk because the exploration maps identify them as runtime,
state authority, storage compatibility, lifecycle composition, modal input, or
render-boundary seams.

### Runtime And Realtime MIDI

Evidence:

- `domain-sequencer-runtime.md` says `SequencerRuntimeService` is the top-level
  runtime orchestrator.
- It also identifies `MidiClockSyncService`, `RealtimeMidiQueue`,
  `SequencerPlaybackService`, `SequencerInternalTimerLane`,
  `SequencerRuntimeSnapshotBank`, and `SequencerMidiEventSink` as the active
  runtime branch.
- `remaining-dark-zones.md` marks hardware/realtime timing under load as still
  unproven.

Verified source seams:

- `src/sequencer/RealtimeMidiQueue.hpp:13` defines the realtime queue.
- `src/sequencer/RealtimeMidiQueue.hpp:15` fixes queue depth and timing
  thresholds.
- `src/sequencer/MidiClockSyncService.hpp:14` defines runtime config.
- `src/sequencer/MidiClockSyncService.hpp:23` defines clock sync service.
- `src/sequencer/SequencerInternalTimerLane.hpp:17` defines the timer lane.
- `src/sequencer/SequencerInternalTimerLane.hpp:43` fixes `TIMER_PERIOD_US`.

Headers to document:

- `src/sequencer/SequencerInternalTimerLane.hpp`
- `src/sequencer/RealtimeMidiQueue.hpp`
- `src/sequencer/MidiClockSyncService.hpp`
- `src/sequencer/SequencerPlaybackService.hpp`
- `src/sequencer/SequencerRuntimeSnapshotBank.hpp`
- `src/sequencer/SequencerMidiEventSink.hpp`
- `src/sequencer/ClockSourceSelector.hpp`
- `src/sequencer/ExternalClockEstimator.hpp`
- `src/sequencer/SequencerRuntimeStateSync.hpp`
- `src/sequencer/SequencerRuntimePerfReporter.hpp`
- `src/sequencer/SequencerPlaybackProfiler.hpp`
- `src/sequencer/RealtimeMidiEvent.hpp`
- `src/sequencer/SequencerTiming.hpp`

Contract themes:

- loop lane vs timer lane ownership;
- who may send MIDI directly and who must queue;
- late/drop/drain budget semantics;
- external clock vs internal transport source ownership;
- snapshot publication and state visibility between runtime and UI.

### State Authority And Lifecycle

Evidence:

- `codebase-map.md` confirms `CoreState` as global standalone state aggregate.
- `domain-macro.md` separates persistent macro page/track config from runtime
  macro values.
- `domain-sequencer-editing.md` identifies sequencer state, snapshots, track
  bank operations, and workflows as the edit authority.
- `domain-settings-data-manager.md` identifies catalog/workflow/executor as the
  Data Manager command authority.

Verified source seams:

- `src/state/CoreState.hpp:139` defines `CoreState`.
- `src/state/CoreStateLifecycle.hpp:9` defines lifecycle operations.
- `src/state/DataManagerWorkflow.hpp:22` defines Data Manager workflow.
- `src/state/DataManagerCatalog.hpp` and `src/state/DataManagerCommandExecutor.hpp`
  are the command catalog/execution seam.

Headers to document or clean:

- `src/state/CoreState.hpp`
- `src/state/CoreStateLifecycle.hpp`
- `src/state/CoreStateBootstrap.hpp`
- `src/state/DataManagerCatalog.hpp`
- `src/state/DataManagerCommandExecutor.hpp`
- `src/state/DataManagerWorkflow.hpp`
- `src/state/DataManagerState.hpp`
- `src/state/DataManagerShortcutPersistence.hpp`
- `src/state/MidiSyncState.hpp`
- `src/state/GlobalSettingsState.hpp`
- `src/state/TrackNavigationState.hpp`
- `src/state/StructureSelectionState.hpp`
- `src/state/StructureClipboardState.hpp`
- `src/state/macro/MacroWorkflow.hpp`
- `src/state/macro/MacroUiState.hpp`
- `src/state/sequencer/SequencerSnapshotOps.hpp`
- `src/state/sequencer/SequencerTrackBankOps.hpp`
- `src/state/sequencer/SequencerTrackBankState.hpp`
- `src/state/sequencer/SequencerUiState.hpp`
- `src/state/sequencer/SequencerQuickControls.hpp`
- `src/state/shared/StructureSlotOps.hpp`

Contract themes:

- which state object owns durable data vs transient UI mode;
- which workflow owns cross-domain side effects;
- when handlers may call domain services instead of writing state directly;
- where deferred sequencer apply/persistence behavior belongs.

### Persistence And Storage Formats

Evidence:

- `domain-persistence.md` confirms the generic slot store plus domain codecs and
  workflows.
- `remaining-dark-zones.md` says binary compatibility and physical SD failure
  behavior remain unproven.

Verified source seams:

- `src/persistence/PersistenceSlotFileStore.hpp:74` defines the generic store.
- `src/persistence/PersistenceSlotFileStore.hpp:76` fixes file format version.
- `src/persistence/PersistenceSlotFileStore.hpp:317` and `:318` assert binary
  header sizes.
- `src/persistence/MacroPersistence.hpp:17` defines macro persistence.
- `src/persistence/MacroPersistence.hpp:178-184` asserts macro payload layout.
- `src/persistence/SequencerPersistence.hpp:15` defines sequencer persistence.
- `src/persistence/SequencerPersistencePayloads.hpp:19` and `:41` define
  pattern/workspace payloads.

Headers to document:

- `src/persistence/PersistenceSlotFileStore.hpp`
- `src/persistence/MacroPersistence.hpp`
- `src/persistence/SequencerPersistence.hpp`
- `src/persistence/SequencerPersistenceCodec.hpp`
- `src/persistence/SequencerPersistencePayloads.hpp`
- `src/state/CoreSettingsCodec.hpp`
- `src/state/CoreSettingsLayout.hpp`
- `src/state/macro/MacroPersistenceWorkflow.hpp`
- `src/state/sequencer/SequencerPersistenceWorkflow.hpp`

Contract themes:

- storage domain and slot ownership;
- binary layout/version expectations;
- CRC/header semantics;
- workflow vs codec vs handler responsibilities;
- compatibility boundary before changing payload structs.

### Context Composition And Presenters

Evidence:

- `codebase-map.md` confirms `StandaloneContext` as lifecycle/composition.
- `lvgl-ui-lifetime-map.md` confirms `StandaloneUiAssembly` owns main UI view
  objects and scopes, `StandaloneOverlayAssembly` owns overlay manager, and
  cleanup order matters.
- `domain-ui-rendering.md` identifies UI assembly as the root layout entry.

Verified source seams:

- `src/context/standalone/StandaloneUiAssembly.hpp:30` defines UI assembly.
- `src/context/standalone/StandaloneOverlayAssembly.hpp:32` defines overlay
  assembly.
- `src/context/standalone/StandaloneFeatureAssembly.hpp:39` defines feature
  assembly.
- `src/context/standalone/StandaloneGlobalHandlerAssembly.hpp:28` defines global
  handler assembly.
- `src/context/standalone/MacroFeatureModule.hpp:45`,
  `SequencerFeatureModule.hpp:41`, and `SettingsFeatureModule.hpp:45` define
  feature modules.

Headers to document:

- `src/context/standalone/StandaloneUiAssembly.hpp`
- `src/context/standalone/StandaloneOverlayAssembly.hpp`
- `src/context/standalone/StandaloneFeatureAssembly.hpp`
- `src/context/standalone/StandaloneGlobalHandlerAssembly.hpp`
- `src/context/standalone/MacroFeatureModule.hpp`
- `src/context/standalone/SequencerFeatureModule.hpp`
- `src/context/standalone/SettingsFeatureModule.hpp`
- `src/context/standalone/MacroOverlayPresenter.hpp`
- `src/context/standalone/SequencerOverlayPresenter.hpp`
- `src/context/standalone/GlobalSettingsOverlayPresenter.hpp`
- `src/context/standalone/DataManagerPresenter.hpp`
- `src/context/standalone/*PresenterFormatters.hpp`
- `src/context/standalone/SequencerEncoderSyncCoordinator.hpp`
- `src/context/standalone/MacroViewActivationContract.hpp`

Contract themes:

- assembly ownership and teardown order;
- view scope vs overlay scope;
- presenter responsibility vs handler/workflow responsibility;
- why feature modules wire handlers/services but do not own runtime ticking.

### Modal Input And Workflows

Evidence:

- `input-binding-map.md` extracted 107 fluent binding blocks.
- It also says the map is lexical, not a full semantic state machine.
- `domain-macro.md`, `domain-sequencer-editing.md`, and
  `domain-settings-data-manager.md` identify workflows/domain services as the
  intended owner of business behavior.

Verified source seams:

- `src/handler/macro/MacroPerformanceModeWorkflow.hpp:16` defines the macro
  performance workflow.
- `src/handler/sequencer/SequencerStructureNavigationWorkflow.hpp:14` defines
  sequencer structure navigation workflow.
- `src/handler/settings/DataManagerHandler.hpp:23` defines Data Manager handler.
- `src/handler/settings/GlobalSettingsHandler.hpp:13` defines global settings
  handler.

Headers to document:

- `src/handler/macro/MacroPerformanceModeWorkflow.hpp`
- `src/handler/macro/MacroStructureWorkflow.hpp`
- `src/handler/macro/MacroPerformanceDomainServices.hpp`
- `src/handler/macro/MacroStructureDomainServices.hpp`
- `src/handler/macro/MacroEditDomainServices.hpp`
- `src/handler/macro/MacroPerformanceHandler.hpp`
- `src/handler/sequencer/SequencerStructureNavigationWorkflow.hpp`
- `src/handler/sequencer/SequencerStructureEditWorkflow.hpp`
- `src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp`
- `src/handler/settings/DataManagerDomainServices.hpp`
- `src/handler/settings/DataManagerHandler.hpp`
- `src/handler/settings/GlobalSettingsDomainServices.hpp`
- `src/handler/settings/GlobalSettingsHandler.hpp`
- `src/handler/common/SharedTrackDomainServices.hpp`
- `src/handler/common/ModalSelectionUtils.hpp`
- `src/handler/common/NavigationUtils.hpp`

Contract themes:

- binding owner vs workflow owner;
- latch/selection mode boundaries;
- when `.when(...)` predicates represent user-visible mode contracts;
- domain service responsibility vs direct `CoreState` writes.

### UI Projection And Render Seams

Evidence:

- `domain-ui-rendering.md` confirms the pipeline:
  context assembly -> root views -> view-model builders -> reusable widgets and
  render logic.
- `lvgl-ui-lifetime-map.md` confirms render timers, view activation, and overlay
  blocking behavior.
- `remaining-dark-zones.md` says UI visual correctness is source-mapped but not
  screenshot-proven.

Verified source seams:

- `src/ui/view/MacroViewModelBuilder.hpp:22` defines macro view-model source.
- `src/ui/sequencer/SequencerViewModelBuilder.hpp:20` defines sequencer
  view-model source.
- `src/ui/sequencer/StepGridRenderPlanner.hpp:10` defines `FrameRenderPlan`.
- `src/ui/view/PausableLvglTimer.hpp:9` defines timer wrapper.
- `src/ui/strip/ContextActionStrip.hpp:59` defines shared action strip widget.

Headers to document:

- `src/ui/view/MacroViewModelBuilder.hpp`
- `src/ui/sequencer/SequencerViewModelBuilder.hpp`
- `src/ui/sequencer/StepGridRenderTypes.hpp`
- `src/ui/sequencer/StepGridFrameLogic.hpp`
- `src/ui/sequencer/StepGridRenderPlanner.hpp`
- `src/ui/sequencer/StepGridRenderLogic.hpp`
- `src/ui/sequencer/StepGridGeometryLogic.hpp`
- `src/ui/sequencer/StepGridLabelLogic.hpp`
- `src/ui/sequencer/StepGridLabelRenderer.hpp`
- `src/ui/sequencer/StepGridWidgets.hpp`
- `src/ui/view/PausableLvglTimer.hpp`
- `src/ui/view/MainViewFrame.hpp`
- `src/ui/strip/ContextActionStrip.hpp`
- `src/ui/common/TrackNavigationStrip.hpp`
- `src/ui/transportbar/ContextSoftkeyBar.hpp`

Contract themes:

- state projection vs widget rendering;
- where frame state is built vs drawn;
- timer ownership and pause/resume semantics;
- shared widget blast radius across Macro and Sequencer views.

## P1 Headers

P1 headers are useful but less urgent because their contracts are narrower,
mostly local, or already partly described by names/tests.

- Public/simple widgets with only visual ownership:
  `MacroKnobWidget.hpp`, `MacroButtonWidget.hpp`, `BaseMacroWidget.hpp`,
  `IMacroWidget.hpp`, `TransportBar.hpp`, `TopBar.hpp`, `SequencerHeaderBar.hpp`,
  `StepGrid.hpp`, `StepPropertyVisuals.hpp`, `StepVisualUtils.hpp`.
- App/platform facade headers:
  `InputAPI.hpp`, `AppLogic.hpp`, `ExtmemAllocator.hpp`, `MidiUtils.hpp`,
  `TimeCompat.hpp`.
- Config headers:
  `App.hpp`, `InputIDs.hpp`, `Timing.hpp`, `Version.hpp`.

Expected treatment:

- Keep comments short.
- Document only non-obvious ownership, platform, memory, ID mapping, or
  cross-module compatibility rules.

## P2 Or Exempt Candidates

Likely exempt:

- generated font data under `src/ui/font/data/*.hpp`;
- raw icon/font lookup tables when they contain no ownership or behavior;
- enum-only aliases such as duplicated `ViewTypes` / `OverlayTypes` wrappers if
  their purpose is obvious or should instead be clarified by naming.

Potential P2 cleanup:

- If `src/app/ViewTypes.hpp`, `src/ui/ViewTypes.hpp`,
  `src/app/OverlayTypes.hpp`, and `src/ui/OverlayTypes.hpp` intentionally bridge
  namespaces, document that bridge once or consolidate later.

## Suggested Work Order

1. Runtime/realtime headers.
   Reason: highest behavioral risk, hardware timing still unproven, and wrong
   ownership can create duplicate MIDI side effects.
2. State + persistence headers.
   Reason: these are the authority and compatibility boundary. They also reduce
   `CoreState` ambiguity before any access-surface refactor.
3. Context/composition headers.
   Reason: they explain where UI, overlays, handlers, and runtime do or do not
   belong.
4. Modal input/workflow headers.
   Reason: input conflicts are known semantic dark zones; contracts should
   describe modes before adding tests.
5. UI render/projection headers.
   Reason: source-level render boundaries are mapped, but visual correctness is
   still unproven; comments should make screenshot failures easier to route.
6. Config/facade/P2 cleanup.
   Reason: useful for polish, but lower risk than runtime/state/composition.

## Definition Of Done

A header is compliant when a reader can answer, without opening a legacy doc:

- what this header owns;
- what it deliberately does not own;
- which state or service boundary it depends on;
- which caller should use it;
- which invariant would be violated by bypassing it.

Do not add boilerplate to every file. If a header is a trivial constant catalog,
generated asset, or leaf render data type, either keep it undocumented or add a
single sentence only if it removes real ambiguity.

## Remaining Verification Needed

- This map does not prove every undocumented header requires a comment; it marks
  candidates based on path, exploration maps, and sampled source checks.
- Before editing each P0 group, read the matching `.cpp` and tests to avoid
  documenting an inferred contract that the implementation does not actually
  enforce.
- Hardware/realtime and UI visual behavior remain partly dark until Sprint 4/5
  validation captures are run.
