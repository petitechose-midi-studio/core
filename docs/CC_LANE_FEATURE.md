# Follow a CC Lane edit through Core

Use this feature as a concrete reading path after [CORE_ARCHITECTURE.md](CORE_ARCHITECTURE.md).
Start with one action: turning a Macro encoder while the CC grid is visible.
The same workflow serves the embedded grid and the CC overlay; their input
scopes remain distinct so a hidden surface cannot consume a gesture.

## Where to make a change

| Change | First file to read |
| --- | --- |
| Encoder, button, tap or hold behavior | [SequencerCcLaneHandler.cpp](../src/handler/sequencer/SequencerCcLaneHandler.cpp), `setupBindings()` |
| Create, edit, clear, delete or transition interaction | [SequencerCcLaneWorkflow.cpp](../src/handler/sequencer/SequencerCcLaneWorkflow.cpp) |
| Lane/event bounds and musical mutations | [SequencerCcLaneDomain.hpp](../src/state/sequencer/SequencerCcLaneDomain.hpp) |
| Destination validity and conflicts | [SequencerCcLaneDomainServices.hpp](../src/handler/sequencer/SequencerCcLaneDomainServices.hpp) and [SequencerCcLaneRouting.cpp](../src/state/sequencer/SequencerCcLaneRouting.cpp) |
| Bank allocation, cloning or publication | [SequencerCcLanePatternOps.hpp](../src/state/sequencer/SequencerCcLanePatternOps.hpp) |
| Undo grouping or failure to admit an edit | [CoreStateSequencerHistoryRecording.cpp](../src/state/CoreStateSequencerHistoryRecording.cpp) |
| Playback interpolation and held values | [SequencerCcLaneRuntime.hpp](../src/sequencer/SequencerCcLaneRuntime.hpp) |
| MIDI arbitration, timing or queue commit | [MidiCcGlobalFrameCoordinator.hpp](../src/sequencer/MidiCcGlobalFrameCoordinator.hpp) |
| Grid layout or drawing | [SequencerCcLaneGrid.cpp](../src/ui/sequencer/SequencerCcLaneGrid.cpp) |
| Persisted CC bytes | [SequencerCcLanePersistenceCodec.hpp](../src/persistence/SequencerCcLanePersistenceCodec.hpp) |
| Construction and injected dependencies | [SequencerFeatureModule.cpp](../src/context/standalone/SequencerFeatureModule.cpp) |

## One encoder edit, in order

1. `SequencerCcLaneHandler::setupBindings()` registers the physical control in
   its owning scope. `mainGridOwnsInput()` admits the embedded grid only in
   `LANE_GRID` mode with no visible overlay. The overlay path requires the CC
   overlay to own input. Both routes call the same workflow.
2. `SequencerCcLaneWorkflow::editVisibleEvent()` resolves the visible step and
   converts the normalized encoder value into the lane's authored range.
   `editFocusedEvent()` handles a relative OPT turn. Both reach
   `setFocusedEventValue_()`.
3. `stageCurrentBank_()` clones the Pattern's bank into a detached EXTMEM owner.
   `setSequencerCcLaneEvent()` validates and mutates that copy. The current
   Pattern is unchanged at this point.
4. `beginCoalescedCcLaneEventEdit()` in
   [SequencerHistoryDomainServices.cpp](../src/handler/sequencer/SequencerHistoryDomainServices.cpp)
   delegates to `CoreState::beginOrContinueSequencerCcLaneEventHistoryCoalescing()`.
   CoreState owns the pending history record and admits the proposed after-image.
   A rejected proposal is not installed.
5. `installSequencerCcLaneBank()` transfers ownership into the Pattern and bumps
   `ccLaneRevision`. `refreshProjection()` derives editor values, action
   availability and feedback. This accepted value is now live; its Undo entry
   can still be pending.

The consequences then follow separate paths:

```text
accepted Pattern bank
    +-> editor projection -> presenter/view -> CC grid drawing
    +-> runtime snapshot -> playback CC frame -> global arbiter -> MIDI queue
    +-> pending/history snapshots -> Undo/Redo
    +-> project snapshot -> persistence codec -> product file transaction
```

These arrows describe ownership boundaries, not synchronous calls from the
encoder. Rendering, runtime publication and file saving have their own cadence.

## Which copy owns what?

| Data | Owner and lifetime |
| --- | --- |
| Authored lanes, events and transitions | `SequencerPatternState::ccLanes`; optional EXTMEM bank owned by the Pattern. The editor's `pattern()` addresses the selected Pattern, not another musical copy. |
| Focus, settings draft, picker, guard and feedback | `SequencerCcLaneUiState` in [SequencerUiState.hpp](../src/state/sequencer/SequencerUiState.hpp); transient interaction state, not the source of playback events. |
| Proposed edit | A local `LaneBankPtr` in the workflow; detached until history admission succeeds, then moved into the Pattern. |
| Before/after images and pending grouping | CoreState history; retained for cancellation/replay and admitted within its resource limits. |
| Playback data | `SequencerRuntimeSnapshotBank`; published snapshots consumed by playback instead of traversing mutable editor state. |
| Last resolved output | Coordinator telemetry, read by [SequencerCcLaneLiveProjection.cpp](../src/handler/sequencer/SequencerCcLaneLiveProjection.cpp); distinct from the authored value. |
| Display props and retained drawing | Presenter/view and `SequencerCcLaneGrid`; derived data, never a second mutation authority. |
| Durable bytes | The project/file persistence pipeline; accepting an edit or recording Undo does not itself save a file. |

## Two history lifetimes, one publication rule

Immediate actions such as create, settings, clear, delete and changed
transitions use `prepareChange_()` and `installPreparedChange_()`. The latter
finishes the detached after-image, calls `canRecordPattern()`, installs the bank,
then records the prepared change. Allocation or admission failure must leave
the proposed musical edit unpublished.

Encoder edits instead use a pending entry so consecutive turns become one Undo
action. Each admitted value is installed immediately. `commitEventEdit()` seals
the group through CoreState; global Undo, track changes and snapshot boundaries
also handle pending history. Do not add a private CC history stack or delay
runtime visibility until a gesture ends.

A transition picker can successfully close when its selected transition is
already applied. This updates feedback without replacing the bank, bumping its
musical revision or adding an Undo entry. A successful interaction is not
necessarily a musical mutation.

## Runtime, display and persistence boundaries

[SequencerRuntimeService.cpp](../src/sequencer/SequencerRuntimeService.cpp) prepares
and publishes a consistent runtime generation. Its
[snapshot bank](../src/sequencer/SequencerRuntimeSnapshotBank.cpp) uses lane source
identity and revision to avoid recopying unchanged payloads. If preparation
fails, playback keeps the previously committed generation and preparation can
retry outside the realtime path.

[SequencerPlaybackService.cpp](../src/sequencer/SequencerPlaybackService.cpp) asks
`SequencerCcLaneRuntime::buildMusicalTickFrame()` for a bounded lane frame at a
new musical tick. The global coordinator combines lane and persistent authors,
resolves destinations and commits MIDI queue work. UI refresh frequency is not
the musical clock. Preserve this boundary when optimizing edit or display code.

[SequencerCcLaneOverlayPresenter.cpp](../src/context/standalone/SequencerCcLaneOverlayPresenter.cpp)
and [SequencerView.cpp](../src/ui/view/SequencerView.cpp) observe state and schedule
rendering. They share the grid component. The workflow's live-value projection
reads the last committed arbiter telemetry; an authored value alone cannot
prove that it won arbitration or reached the device's MIDI output.

The CC codec defines explicit bytes independent of C++ padding.
[SequencerPersistenceEnvelope.cpp](../src/persistence/SequencerPersistenceEnvelope.cpp)
embeds that record in pattern/project content. File transactions remain in
persistence; gestures and widgets do not write storage directly.

## Tests to read before editing

| Contract | Existing suite |
| --- | --- |
| Mutation bounds, routing, musical rules and bounded lane runtime | [test_SequencerCcLaneDomain](../test/test_SequencerCcLaneDomain/test_main.cpp) |
| Publication failures, grouped edits, Undo, track/snapshot boundaries, scope/gesture ownership and unchanged transition | [test_SequencerCcLaneWorkflow](../test/test_SequencerCcLaneWorkflow/test_main.cpp) |
| Projection and event selection | [test_SequencerCcLaneProjectionOps](../test/test_SequencerCcLaneProjectionOps/test_main.cpp) |
| Arbitration, runtime emission and queue behavior | [test_MidiCcGlobalFrameCoordinator](../test/test_MidiCcGlobalFrameCoordinator/test_main.cpp) |
| Lane frame integration with playback | [test_SequencerPlaybackService](../test/test_SequencerPlaybackService/test_main.cpp) |
| Explicit bytes and invalid-record rejection | [test_SequencerCcLanePersistence](../test/test_SequencerCcLanePersistence/test_main.cpp) |

For integrated input and presentation, use the `cc-lane-*` and
`cc-grid-playback.ux` workflows in
[sdl/integration/workflows/sequencer/editing](../sdl/integration/workflows/sequencer/editing).
Compare semantic dispatches and captures. Transport and hold-feedback pixels
depend on capture timing; investigate differences rather than treating every
whole-image mismatch as a domain regression.

Run the architecture gate and full native suite, then the firmware memory gates
as described in [ARCHITECTURE_REVIEW_RULES.md](ARCHITECTURE_REVIEW_RULES.md).
Changes to realtime timing also need focused hardware measurements. A smaller
workflow alone is not evidence of a CPU improvement.
