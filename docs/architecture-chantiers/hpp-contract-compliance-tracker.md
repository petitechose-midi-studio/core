# HPP Contract Compliance Tracker

Purpose: track progress while reviewing and standardizing `.hpp` contract
comments.

Use this tracker for progress only. The rationale and prioritization live in
`hpp-contract-compliance-map.md`; durable contract text belongs in the relevant
`.hpp`.

## Status Legend

- `TODO`: identified, not reviewed yet.
- `VERIFY`: read the `.cpp`, tests, or exploration map before writing the
  contract.
- `PATCHED`: `.hpp` contract comment added or cleaned.
- `SKIP`: intentionally left undocumented, with a short reason.
- `BLOCKED`: cannot write a truthful contract without additional runtime,
  visual, hardware, or test evidence.

## Update Rules

- Touch `.hpp` files first; keep `.cpp` comments minimal and local.
- Write contract comments only where they remove ownership, lifecycle,
  persistence, runtime, state, input, or render-boundary ambiguity.
- Before moving a row to `PATCHED`, confirm the comment does not repeat stale
  docs and does not introduce a contract not enforced by code.
- Before moving a row to `SKIP`, leave a short reason in `Notes`.
- Generated font data starts as `SKIP` unless it gains behavior or ownership.

## Batch Order

1. Runtime/realtime headers.
2. State + persistence headers.
3. Context/composition headers.
4. Modal input/workflow headers.
5. UI projection/render headers.
6. Config/facade/P1/P2 cleanup.

## P0 Runtime And Realtime MIDI

| Priority | Domain | Header | Evidence | Needed contract | Status | Notes |
|---|---|---|---|---|---|---|
| P0 | runtime | `src/sequencer/SequencerRuntimeService.hpp` | `domain-sequencer-runtime.md`; class at header line 33 | Single owner, narrow `StateRefs`, no second runtime path | PATCHED | Sprint 0 comment added. |
| P0 | runtime | `src/sequencer/SequencerInternalTimerLane.hpp` | `domain-sequencer-runtime.md`; timer lane at line 17; `TIMER_PERIOD_US` at line 43 | Timer lane ownership, queue drain responsibility, profiling boundary | PATCHED | `.cpp` verified before patching. |
| P0 | runtime | `src/sequencer/RealtimeMidiQueue.hpp` | `domain-sequencer-runtime.md`; queue at line 13; depth/thresholds at lines 15-18 | Due-event ordering, late/drop policy, drain budget contract | PATCHED | `.cpp` and queue tests verified. |
| P0 | runtime | `src/sequencer/MidiClockSyncService.hpp` | `domain-sequencer-runtime.md`; config at line 14; service at line 23 | Internal/external clock ownership and UI projection boundary | PATCHED | `.cpp` and sync tests verified. |
| P0 | runtime | `src/sequencer/SequencerPlaybackService.hpp` | `domain-sequencer-runtime.md`; playback service | Playback from snapshots, queue-only MIDI output rule | PATCHED | `.cpp` verified. |
| P0 | runtime | `src/sequencer/SequencerRuntimeSnapshotBank.hpp` | `domain-sequencer-runtime.md`; snapshot bridge | Snapshot publication and runtime/UI state visibility | PATCHED | `.cpp` and snapshot tests verified. |
| P0 | runtime | `src/sequencer/SequencerMidiEventSink.hpp` | `domain-sequencer-runtime.md`; event sink | Sequencer engine event sink to realtime queue boundary | PATCHED | `.cpp` and event sink tests verified. |
| P0 | runtime | `src/sequencer/ClockSourceSelector.hpp` | `domain-sequencer-runtime.md`; clock source selection | Source selection policy and fallback ownership | PATCHED | `.cpp` verified. |
| P0 | runtime | `src/sequencer/ExternalClockEstimator.hpp` | `domain-sequencer-runtime.md`; external clock telemetry | External clock measurement and telemetry contract | PATCHED | `.cpp` verified. |
| P0 | runtime | `src/sequencer/SequencerRuntimeStateSync.hpp` | runtime branch inventory | Runtime-to-state sync responsibility | PATCHED | `.cpp` verified. |
| P0 | runtime | `src/sequencer/SequencerRuntimePerfReporter.hpp` | runtime branch inventory; hardware dark zone | Perf window reporting and hardware validation boundary | PATCHED | `.cpp` verified. |
| P0 | runtime | `src/sequencer/SequencerPlaybackProfiler.hpp` | runtime branch inventory | Profiling scope and non-behavioral nature | PATCHED | `.cpp` verified. |
| P0 | runtime | `src/sequencer/RealtimeMidiEvent.hpp` | realtime queue branch | Event data shape and ordering assumptions | PATCHED | Queue/event sink behavior verified. |
| P0 | runtime | `src/sequencer/SequencerTiming.hpp` | runtime timing helper | Timing math ownership and caller expectations | PATCHED | Clock sync usage verified. |

## P0 State Authority And Lifecycle

| Priority | Domain | Header | Evidence | Needed contract | Status | Notes |
|---|---|---|---|---|---|---|
| P0 | state | `src/state/CoreState.hpp` | `codebase-map.md`; `CoreState` at line 139 | App-level state authority; remove legacy wording | PATCHED | `.cpp` lifecycle verified; legacy comparison wording removed. |
| P0 | state | `src/state/CoreStateLifecycle.hpp` | lifecycle operations at line 9 | Main-loop update/flush/reset authority | PATCHED | `.cpp` verified before patching. |
| P0 | state | `src/state/CoreStateBootstrap.hpp` | UI/lifecycle maps; overlay signal registration | Bootstrap-only signal/default registration boundary | PATCHED | `.cpp` verified before patching. |
| P0 | state | `src/state/DataManagerCatalog.hpp` | `domain-settings-data-manager.md` | Command catalog authority and shortcut defaults | PATCHED | Header table is the single source for labels/domains/defaults. |
| P0 | state | `src/state/DataManagerCommandExecutor.hpp` | `domain-settings-data-manager.md` | Command execution vs handler responsibility | PATCHED | `.cpp` dispatch verified. |
| P0 | state | `src/state/DataManagerWorkflow.hpp` | workflow at line 22 | Workflow hooks, shortcut mutation, command dispatch | PATCHED | `.cpp` and command executor verified. |
| P0 | state | `src/state/DataManagerState.hpp` | Data Manager domain map | Transient overlay/dialog state ownership | PATCHED | Header state shape verified. |
| P0 | state | `src/state/DataManagerShortcutPersistence.hpp` | settings/data-manager map | Shortcut persistence boundary | PATCHED | Workflow/settings path verified. |
| P0 | state | `src/state/MidiSyncState.hpp` | runtime clock map | UI/system state for sync indicators | PATCHED | Runtime projection fields verified against clock services. |
| P0 | state | `src/state/GlobalSettingsState.hpp` | settings map | Global settings state vs handler workflow | PATCHED | Header state shape verified. |
| P0 | state | `src/state/TrackNavigationState.hpp` | macro/sequencer shared track maps | Shared track navigation state boundary | PATCHED | Shared track state authority verified in CoreState. |
| P0 | state | `src/state/StructureSelectionState.hpp` | sequencer editing map | Structure selection transient state | PATCHED | Shared selection primitives verified. |
| P0 | state | `src/state/StructureClipboardState.hpp` | sequencer editing map | Structure clipboard ownership | PATCHED | Clipboard snapshot fields verified. |
| P0 | state | `src/state/macro/MacroWorkflow.hpp` | `domain-macro.md` | Canonical macro runtime/config mutation workflow | PATCHED | `.cpp` verified before patching. |
| P0 | state | `src/state/macro/MacroUiState.hpp` | macro domain map | Macro transient UI mode ownership | PATCHED | Header state shape verified. |
| P0 | state | `src/state/sequencer/SequencerSnapshotOps.hpp` | `domain-sequencer-editing.md` | Pattern/page mutation authority | PATCHED | `.cpp` sanitization and revision behavior verified. |
| P0 | state | `src/state/sequencer/SequencerTrackBankOps.hpp` | `domain-sequencer-editing.md` | Track-bank mutation authority | PATCHED | `.cpp` active editor/bank copy behavior verified. |
| P0 | state | `src/state/sequencer/SequencerTrackBankState.hpp` | `domain-sequencer-editing.md` | Multi-track durable/runtime state boundary | PATCHED | `.cpp` sanitization/reset behavior verified. |
| P0 | state | `src/state/sequencer/SequencerUiState.hpp` | `domain-sequencer-editing.md` | Sequencer UI/edit-mode state vs musical data | PATCHED | Header state shape verified. |
| P0 | state | `src/state/sequencer/SequencerQuickControls.hpp` | sequencer quick controls map | Quick-control selection/value model | PATCHED | Header display-order table verified. |
| P0 | state | `src/state/shared/StructureSlotOps.hpp` | macro/sequencer structure maps | Shared slot mechanics boundary | PATCHED | Header-only mask/navigation helpers verified. |

## P0 Persistence And Storage Formats

| Priority | Domain | Header | Evidence | Needed contract | Status | Notes |
|---|---|---|---|---|---|---|
| P0 | persistence | `src/persistence/PersistenceSlotFileStore.hpp` | `domain-persistence.md`; store at line 74; format version at line 76 | Slot file layout, CRC/header, save-counter semantics | PATCHED | Store tests verified before patching. |
| P0 | persistence | `src/persistence/MacroPersistence.hpp` | `domain-persistence.md`; payload asserts lines 178-184 | Macro library payload ownership | PATCHED | Payload/static assert contract verified. |
| P0 | persistence | `src/persistence/SequencerPersistence.hpp` | `domain-persistence.md`; class at line 15 | Sequencer pattern/set library storage boundary | PATCHED | Codec and workflow paths verified. |
| P0 | persistence | `src/persistence/SequencerPersistenceCodec.hpp` | persistence map | Codec ownership vs workflow/handler | PATCHED | `.cpp` sanitizer/apply behavior verified. |
| P0 | persistence | `src/persistence/SequencerPersistencePayloads.hpp` | payload structs at lines 19 and 41 | Binary layout compatibility warning | PATCHED | Packed payload/static assert contract added. |
| P0 | persistence | `src/persistence/StorageRecoveryMachine.hpp` | Sprint 4 recovery strategy; native recovery tests | Pure SD recovery state machine boundary | PATCHED | Debounce, safe-point deferral, retry, and removed-during-pending behavior verified. |
| P0 | persistence | `src/state/CoreSettingsCodec.hpp` | settings persistence map | Settings codec/default shortcut boundary | PATCHED | Codec boundary verified; v1 compatibility and unavailable-storage status covered. |
| P0 | persistence | `src/state/CoreSettingsLayout.hpp` | settings persistence map | Settings layout compatibility | PATCHED | Offset/version contract added. |
| P0 | persistence | `src/state/macro/MacroPersistenceWorkflow.hpp` | `domain-macro.md`; persistence workflow | Macro save/load workflow boundary | PATCHED | `.cpp` runtime-sync/load side effects verified. |
| P0 | persistence | `src/state/sequencer/SequencerPersistenceWorkflow.hpp` | sequencer editing/persistence maps | Deferred apply and set/pattern workflow boundary | PATCHED | `.cpp` deferred apply and merge/replace behavior verified. |

## P0 Context Composition And Presenters

| Priority | Domain | Header | Evidence | Needed contract | Status | Notes |
|---|---|---|---|---|---|---|
| P0 | context | `src/context/StandaloneContext.hpp` | `codebase-map.md`; `lvgl-ui-lifetime-map.md` | Lifecycle/composition root; runtime outside context | PATCHED | Existing useful contract. May need final wording pass. |
| P0 | context | `src/context/standalone/StandaloneUiAssembly.hpp` | UI lifetime map; class at line 30 | Main UI ownership, view scopes, render timer ownership | PATCHED | `.cpp` LVGL ownership and timer behavior verified. |
| P0 | context | `src/context/standalone/StandaloneOverlayAssembly.hpp` | UI lifetime map; class at line 32 | Overlay controller/scope ownership | PATCHED | `.cpp` overlay cleanup/scope behavior verified. |
| P0 | context | `src/context/standalone/StandaloneFeatureAssembly.hpp` | feature assembly map; class at line 39 | Feature wiring boundary, not runtime owner | PATCHED | `.cpp` feature wiring and forwarding behavior verified. |
| P0 | context | `src/context/standalone/StandaloneGlobalHandlerAssembly.hpp` | input/lifecycle maps | Global handler wiring boundary | PATCHED | `.cpp` transport/view-switcher ownership verified. |
| P0 | context | `src/context/standalone/MacroFeatureModule.hpp` | macro domain map; class at line 45 | Macro handlers/services/presenter ownership | PATCHED | `.cpp` overlay/presenter/handler wiring verified. |
| P0 | context | `src/context/standalone/SequencerFeatureModule.hpp` | sequencer maps; class at line 41 | Sequencer handlers/presenter ownership, no runtime tick | PATCHED | `.cpp` confirms UI/input ownership, no playback runtime tick. |
| P0 | context | `src/context/standalone/SettingsFeatureModule.hpp` | settings map; class at line 45 | Settings/Data Manager handler ownership | PATCHED | `.cpp` presenter/handler/service wiring verified. |
| P0 | context | `src/context/standalone/MacroOverlayPresenter.hpp` | UI lifetime map | Macro overlay render responsibility | PATCHED | `.cpp` signal watcher and render-only behavior verified. |
| P0 | context | `src/context/standalone/SequencerOverlayPresenter.hpp` | UI lifetime map | Sequencer overlay render responsibility | PATCHED | `.cpp` render-only step edit behavior verified. |
| P0 | context | `src/context/standalone/GlobalSettingsOverlayPresenter.hpp` | settings/UI maps | Global settings overlay render responsibility | PATCHED | `.cpp` signal watcher/render behavior verified. |
| P0 | context | `src/context/standalone/DataManagerPresenter.hpp` | Data Manager/UI maps | Data Manager overlay/softkey render responsibility | PATCHED | `.cpp` overlay/dialog/softkey render behavior verified. |
| P0 | context | `src/context/standalone/*PresenterFormatters.hpp` | presenter inventory | Formatting-only seam; no state mutation | PATCHED | Concrete formatter headers documented as pure render-data builders. |
| P0 | context | `src/context/standalone/SequencerEncoderSyncCoordinator.hpp` | sequencer/context maps | Encoder sync ownership and timing | PATCHED | `.cpp` encoder config/position sync behavior verified. |
| P0 | context | `src/context/standalone/MacroViewActivationContract.hpp` | macro/lifecycle maps; useful line comment at line 9 | Standardize macro activation sync contract | PATCHED | Inline comment converted to header contract. |
| P0 | context | `src/context/standalone/ActiveViewLifecyclePlan.hpp` | UI lifetime map | View activation side-effect order | PATCHED | Sprint 0 contract added. |
| P0 | context | `src/context/standalone/StandaloneSequencerRuntimeGate.hpp` | runtime map | Pre-context hook decision rule | PATCHED | Sprint 0 contract added. |

## P0 Modal Input And Workflows

| Priority | Domain | Header | Evidence | Needed contract | Status | Notes |
|---|---|---|---|---|---|---|
| P0 | input | `src/handler/macro/MacroPerformanceModeWorkflow.hpp` | macro map; workflow at line 16 | Clutch/quick-control mode ownership | PATCHED | `.cpp` modal state and encoder behavior verified. |
| P0 | input | `src/handler/macro/MacroStructureWorkflow.hpp` | macro map | Macro structure edit workflow boundary | PATCHED | `.cpp` preview/selection/clipboard behavior verified. |
| P0 | input | `src/handler/macro/MacroPerformanceDomainServices.hpp` | macro map | Services delegate to MacroWorkflow | PATCHED | `.cpp` CoreState bridge behavior verified. |
| P0 | input | `src/handler/macro/MacroStructureDomainServices.hpp` | macro map | Page/track structure services | PATCHED | `.cpp` mask mutation/persistence refresh verified. |
| P0 | input | `src/handler/macro/MacroEditDomainServices.hpp` | macro map | Macro edit service boundary | PATCHED | `.cpp` MacroWorkflow delegation verified. |
| P0 | input | `src/handler/macro/MacroPerformanceHandler.hpp` | input map | Binding owner vs workflow owner | PATCHED | `.cpp` input binding vs workflow split verified. |
| P0 | input | `src/handler/sequencer/SequencerStructureNavigationWorkflow.hpp` | sequencer/input maps; class at line 14 | Structure navigation modes and selection state | PATCHED | `.cpp` navigation/preview/selection behavior verified. |
| P0 | input | `src/handler/sequencer/SequencerStructureEditWorkflow.hpp` | sequencer editing map | Structure mutation workflow; snapshot ops delegate | PATCHED | `.cpp` snapshot/clipboard/track-bank behavior verified. |
| P0 | input | `src/handler/sequencer/SequencerStructureTrackOps.hpp` | Sprint 3 structure mechanics | Shared sequencer-only track creation primitive | PATCHED | Header-only helper verified through navigation create and paste-to-add-slot tests. |
| P0 | input | `src/handler/sequencer/SequencerPatternQuickControlsHandler.hpp` | input/sequencer maps | Quick-control overlay binding owner | PATCHED | `.cpp` cancel/offset snapshot behavior verified. |
| P0 | input | `src/handler/settings/DataManagerDomainServices.hpp` | settings map | Data Manager services to workflow/executor | PATCHED | `.cpp` workflow facade behavior verified. |
| P0 | input | `src/handler/settings/DataManagerHandler.hpp` | input map; handler at line 23 | Data Manager modal/dialog binding owner | PATCHED | `.cpp` modal command flow verified. |
| P0 | input | `src/handler/settings/GlobalSettingsDomainServices.hpp` | settings map | Global setting application services | PATCHED | `.cpp` choice mapping/persistence commit verified. |
| P0 | input | `src/handler/settings/GlobalSettingsHandler.hpp` | input map; handler at line 13 | Settings overlay binding owner | PATCHED | `.cpp` overlay/selector input flow verified. |
| P0 | input | `src/handler/common/SharedTrackDomainServices.hpp` | macro/sequencer shared track maps | Shared track operation boundary | PATCHED | `.cpp` CoreState operation hook verified. |
| P0 | input | `src/handler/common/ModalSelectionUtils.hpp` | input map | Mechanical modal selection helper scope | PATCHED | Header-only helpers verified. |
| P0 | input | `src/handler/common/NavigationUtils.hpp` | input map | Mechanical navigation helper scope | PATCHED | Header-only helpers verified. |

## P0 UI Projection And Render Seams

| Priority | Domain | Header | Evidence | Needed contract | Status | Notes |
|---|---|---|---|---|---|---|
| P0 | UI | `src/ui/view/MacroViewModelBuilder.hpp` | UI map; source at line 22 | State projection boundary before widgets | PATCHED | `.cpp` projection-only behavior verified. |
| P0 | UI | `src/ui/sequencer/SequencerViewModelBuilder.hpp` | UI map; source at line 20 | Sequencer prop projection boundary | PATCHED | `.cpp` projection-only behavior verified. |
| P0 | UI | `src/ui/sequencer/StepGridRenderTypes.hpp` | UI map | Frame/diff/cache data ownership | PATCHED | Render data ownership documented. |
| P0 | UI | `src/ui/sequencer/StepGridFrameLogic.hpp` | UI map | State-to-frame conversion boundary | PATCHED | `.cpp` frame projection verified. |
| P0 | UI | `src/ui/sequencer/StepGridRenderPlanner.hpp` | UI map; `FrameRenderPlan` at line 10 | Diff planning vs drawing boundary | PATCHED | `.cpp` dirty planning behavior verified. |
| P0 | UI | `src/ui/sequencer/StepGridRenderLogic.hpp` | UI map | Drawing logic boundary | PATCHED | `.cpp` pure visual/diff behavior verified. |
| P0 | UI | `src/ui/sequencer/StepGridGeometryLogic.hpp` | UI map | Geometry calculation boundary | PATCHED | `.cpp` pure geometry behavior verified. |
| P0 | UI | `src/ui/sequencer/StepGridLabelLogic.hpp` | UI map | Label model boundary | PATCHED | `.cpp` label presentation behavior verified. |
| P0 | UI | `src/ui/sequencer/StepGridLabelRenderer.hpp` | UI map | Label rendering boundary | PATCHED | `.cpp` cache-aware LVGL updates verified. |
| P0 | UI | `src/ui/sequencer/StepGridWidgets.hpp` | UI map | Child widget ownership | PATCHED | `.cpp` LVGL object creation behavior verified. |
| P0 | UI | `src/ui/view/PausableLvglTimer.hpp` | LVGL lifetime map; class at line 9 | LVGL timer RAII and pause/resume semantics | PATCHED | `.cpp` pause/resume/delete behavior verified. |
| P0 | UI | `src/ui/view/MainViewFrame.hpp` | UI map | Shared root frame ownership | PATCHED | `.cpp` frame container ownership verified. |
| P0 | UI | `src/ui/strip/ContextActionStrip.hpp` | UI map; widget at line 59 | Shared action strip blast radius | PATCHED | `.cpp` cache/hold timer behavior verified. |
| P0 | UI | `src/ui/common/TrackNavigationStrip.hpp` | UI map | Shared track strip render responsibility | PATCHED | `.cpp` geometry/cache render behavior verified. |
| P0 | UI | `src/ui/transportbar/ContextSoftkeyBar.hpp` | UI map | Context softkey render ownership | PATCHED | `.cpp` label/visibility behavior verified. |

## P1 Queue

| Priority | Domain | Header group | Status | Notes |
|---|---|---|---|---|
| P1 | UI widgets | `MacroKnobWidget.hpp`, `MacroButtonWidget.hpp`, `BaseMacroWidget.hpp`, `IMacroWidget.hpp` | TODO | Add only if ownership/lifetime is unclear. |
| P1 | UI widgets | `TransportBar.hpp`, `TopBar.hpp`, `SequencerHeaderBar.hpp`, `StepGrid.hpp` | TODO | Prefer short comments; avoid visual prose. |
| P1 | UI helpers | `StepPropertyVisuals.hpp`, `StepVisualUtils.hpp` | TODO | Likely tiny comments only. |
| P1 | app/api | `InputAPI.hpp`, `AppLogic.hpp`, `ExtmemAllocator.hpp`, `MidiUtils.hpp`, `TimeCompat.hpp` | TODO | Document public/facade or memory/platform assumptions. |
| P1 | config | `App.hpp`, `InputIDs.hpp`, `Timing.hpp`, `Version.hpp` | TODO | Document ID/timing compatibility only if useful. |

## P2 Or Exempt Queue

| Priority | Domain | Header group | Status | Notes |
|---|---|---|---|---|
| P2 | generated assets | `src/ui/font/data/*.hpp` | SKIP | Generated font data; no behavioral contract. |
| P2 | enum bridges | `src/app/ViewTypes.hpp`, `src/ui/ViewTypes.hpp`, `src/app/OverlayTypes.hpp`, `src/ui/OverlayTypes.hpp` | TODO | Decide whether to document namespace bridge or consolidate later. |
| P2 | icons/fonts | `StandaloneIcons.hpp`, `StandaloneFonts.hpp` | TODO | Likely one-line registry/asset comments only. |

## Progress Snapshot

| Priority | Total tracked | PATCHED | VERIFY | TODO | SKIP | BLOCKED |
|---|---:|---:|---:|---:|---:|---:|
| P0 | 94 | 94 | 0 | 0 | 0 | 0 |
| P1 groups | 5 | 0 | 0 | 5 | 0 | 0 |
| P2 groups | 3 | 0 | 0 | 2 | 1 | 0 |

Update this snapshot after each batch.
