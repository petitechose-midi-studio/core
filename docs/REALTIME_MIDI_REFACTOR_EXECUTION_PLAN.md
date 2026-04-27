# Realtime MIDI Refactor Execution Plan

> Date: 2026-04-27
> Scope: `midi-studio/core` plus the required `open-control` MIDI/sequencer seams
> Status: active execution plan
> Purpose: remove ambiguity before changing the realtime sequencer/output path

---

## 1. Objective

The goal is not only to make the UI faster.

The goal is to make this invariant true:

- UI overload may delay visual updates.
- UI overload may not delay, bunch, reorder, or phase-shift sequencer clock, scheduling, or outgoing MIDI.

This plan treats maintainability, safety, and performance as the same problem:

- one authoritative path per concern
- no hidden parallel path after migration
- measurable realtime behavior
- small named modules with reviewable responsibility
- no speculative UI cleanup before the musical path is protected

---

## 2. Current Codebase Validation

This section validates the plan against the live codebase.

| Claim | Current evidence | Assessment |
| --- | --- | --- |
| Standalone runtime ownership is now singular. | `main.cpp` owns `standaloneSequencerRuntime` and updates it from `registerPreContextUpdateHook(...)`. | Good baseline. Preserve. |
| Internal master timing has a named timer lane. | `SequencerInternalTimerLane` owns timer start/stop, high-resolution transport advancement, MIDI clock burst limiting, playback scheduling, output drain, and timer profiling. | Good baseline. Runtime service now decides ownership without carrying timer mechanics. |
| Framework app and realtime time are separated. | `oc::time::millis()` handles application time; `oc::time::micros32()` and `signedDeltaUs(...)` handle realtime deadlines. | Good baseline. Preserve API separation. |
| A Teensy high-resolution clock already exists. | `../../open-control/hal-teensy/src/oc/hal/teensy/HighResolutionClock.*` provides monotonic `micros64()`; `InternalTransportClock` uses it on Arduino. | Good baseline. Keep it HAL-owned. |
| Core realtime time is bridged through framework time. | `src/config/TimeCompat.hpp` prefers `oc::time` providers and keeps local platform reads for bootstrap/tests. | Acceptable bridge. Avoid adding new time helpers. |
| Core runtime no longer owns low-level timer/IRQ primitives directly. | `SequencerRuntimeService` uses `oc::realtime::PeriodicTimer`; snapshot/timer critical sections use `oc::realtime::InterruptGuard`. HAL drivers keep HAL-local locking where they own hardware queues. | Good baseline. Keep hardware primitives outside core policy code. |
| The scheduler emits tick-domain events. | `open-control/note/.../NoteScheduler.hpp::processUntil(...)` emits `SequencerEvent` through `ISequencerEventSink`. | Good baseline. Preserve transport independence. |
| Core maps sequencer events to a realtime queue. | `SequencerMidiEventSink` writes `RealtimeMidiEvent` objects to `RealtimeMidiQueue`. | Good baseline. This is the single standalone note output path. |
| USB MIDI output has a backend queue with bounded drain. | `UsbMidi` has `OUTPUT_QUEUE_CAPACITY`, `pollInput()`, and `serviceOutput(budgetUs)`. | Good baseline. Deadline policy remains in core queue. |
| USB MIDI input/external clock is explicitly separated from output drain. | `OpenControlApp::update()` calls `midi_->pollInput()` before pre-context hooks and drains output with budgets around context work. | External clock guarantee remains separate from internal master timing. |
| `OpenControlApp` drains MIDI with explicit budgets. | `OpenControlApp::update()` calls `midi_->serviceOutput(budgetUs)` before and after `contexts_->update()`. | Good baseline. Tune budgets with captures. |
| Sequencer UI churn is isolated from realtime correctness. | `SequencerView` keeps playhead updates on `StepGrid`; `SequencerHeaderBar` no longer observes playhead ticks; `StepGridRenderLogic` treats probability-cycle masking as visual overlay state, not step data. | Good baseline. Keep UI work outside timing guarantees. |
| `CoreState` is broad. | Many assemblies/workflows depend on `CoreState&`. | Runtime consumes narrow snapshots, not root state. |

- Next path: validate timing on hardware under UI stress and tune budgets from captures.
- UI cleanup follows measurable interaction bottlenecks only.

---

## 3. Decisions

### 3.1 What is guaranteed

Firmware-side guarantee:

- internal musical phase is not derived from UI loop cadence
- scheduler deadlines are not delayed by LVGL/context work
- outgoing MIDI work is queued/drained by deadline policy
- missed deadlines are counted, not hidden

USB product guarantee:

- USB timing is firmware-correct and measured
- remaining host/USB packetization jitter is bounded and documented
- do not promise mathematically perfect host-visible USB timing

DIN/UART product guarantee:

- DIN/UART is the candidate for the strongest end-to-end timing guarantee
- add it only after the queue/deadline model exists, unless hardware release scope forces it sooner

### 3.2 Where the new event type lives

Decision:

- musical scheduling events live in `open-control/note` in tick domain
- firmware realtime MIDI events live in `midi-studio/core` in absolute microsecond domain
- transport-specific drain policy lives in `open-control/hal-teensy`

Why:

- `open-control/note` should remain transport-agnostic
- `core` knows the standalone runtime timeline and track context
- HAL owns USB/DIN backend behavior

### 3.3 Standalone output path

Decision:

- the standalone runtime must migrate to the new producer/queue path
- after migration, standalone output is only `producer -> realtime queue -> backend drain`
- standalone runtime output is emitted through the event sink

### 3.4 Late event policy

Initial policy:

- on time: send at deadline
- small lateness: send immediately and count
- large lateness: drop and count
- repeated lateness or overflow: request resync/panic according to an explicit policy

Initial thresholds should be constants, not magic numbers:

- `LATE_SEND_THRESHOLD_US`
- `DROP_THRESHOLD_US`
- `MAX_QUEUE_DEPTH`
- `MAX_DRAIN_BUDGET_US`

The exact numeric values should be tuned with hardware captures, but the policy must exist before broad implementation.

### 3.5 Snapshot ownership

Decision:

- UI/domain state stays mutable in `CoreState`
- runtime consumes immutable pattern/track snapshots
- snapshot commit is explicit and bounded
- the scheduler never reads `CoreState` directly

Current double-buffering in `SequencerRuntimeService` is a good base, but it should be pulled into a named owner once the queue path lands.

### 3.6 Time-domain ownership

Decision:

- `oc::time::millis()` remains the application/UI time source.
- realtime musical deadlines use an explicit microsecond-domain primitive in `open-control`, not ad hoc Arduino calls in core.
- deadline comparisons use wrap-safe 32-bit microsecond arithmetic for short windows.
- 64-bit microsecond time is reserved for diagnostics, long captures, or host tooling, not required in the ISR hot path.
- `core::time_compat` is a bridge to `oc::time` with local fallback for tests/bootstrap.
- `IntervalTimer`, IRQ masking, and similar platform primitives should be isolated behind framework/HAL seams before they spread further.

Why:

- millisecond time is too coarse for MIDI deadlines and jitter accounting
- mutable 64-bit clock wrappers are useful but awkward as a shared ISR/loop dependency
- wrap-safe `uint32_t` deadline math is deterministic, cheap, testable, and enough for bounded realtime windows
- the framework already owns `oc::time`, `IMidi`, `OpenControlApp`, and Teensy HAL construction, so this contract belongs there

Initial deadline math contract:

```cpp
inline int32_t signedDeltaUs(uint32_t nowUs, uint32_t deadlineUs) {
    return static_cast<int32_t>(nowUs - deadlineUs);
}
```

Rule:

- positive delta means late
- zero or negative delta means on time or early
- code must not compare absolute microsecond timestamps with `<` / `>` across wrap boundaries

---

## 4. Target Architecture

```text
UI / handlers / persistence
        |
        v
Sequencer edit state
        |
        v explicit commit
Runtime snapshot bank
        |
        v
Realtime clock ----------+
        |                |
        v                |
Clock lane --------------+
        |                |
        v                |
Tick-domain scheduler    |
        |                |
        v                |
Scheduled note events    |
        | tick -> us     |
        v                |
Realtime MIDI queue      |
        |                |
        v                |
Backend drain policy     |
        |
        v
USB MIDI / DIN MIDI
```

Forbidden in clock/scheduler/output lanes:

- LVGL calls
- UI state writes
- storage I/O
- dynamic allocation
- per-event logging
- broad event bus emission
- reads from live mutable UI state

Allowed:

- fixed-capacity queues
- bounded counters
- immutable snapshot reads
- cheap telemetry copied out to the UI lane later

### 4.1 Target time model

The codebase should have two explicit time domains:

| Domain | API owner | Resolution | Use cases | Forbidden uses |
| --- | --- | --- | --- | --- |
| Application time | `oc::time::millis()` | milliseconds | UI, debounce, persistence, logs, display updates | MIDI event deadlines |
| Realtime time | `oc::time` / HAL realtime primitive | microseconds | clock phase, scheduler deadlines, late/drop policy, drain budget | UI animation, persistence debounce |

The realtime primitive should be exposed by `open-control` because both core and MIDI HAL need it.

Preferred initial shape:

```cpp
namespace oc::time {
uint32_t micros32();
bool isMicrosConfigured();
int32_t signedDeltaUs(uint32_t nowUs, uint32_t deadlineUs);
}
```

Fallback behavior:

- native builds use `std::chrono::steady_clock`
- Teensy builds use a lightweight microsecond provider
- embedded builds return `0` until the HAL registers the provider; realtime firmware construction must configure it before use

---

## 5. Work Items

### CA-016 - Freeze Realtime Contract And Counters

Problem:

- current docs describe desired behavior, but the runtime does not yet expose enough counters to prove regressions or improvements

Scope:

- `src/sequencer/SequencerRuntimeService.*`
- `src/sequencer/SequencerPlaybackService.*`
- `src/sequencer/SequencerRuntimeStateSync.*`
- `docs/REALTIME_MIDI_*`

Deliverables:

- `RealtimeTimingCounters` or equivalent small struct
- counters for:
  - runtime update max
  - timer callback max
  - scheduler update max
  - backend drain max
  - tick jump max
  - late note count
  - dropped note count, once queue/drop policy exists
  - queue high-water mark, once queue exists
- no per-event logs
- documentation of thresholds and stress scenarios

Validation:

- `pio test -e native -f test_MidiClockSyncService`
- `pio test -e native -f test_StandaloneSequencerRuntimeGate`
- `pio run -e dev`
- hardware stress capture when available

Success criteria:

- a reviewer can distinguish UI frame cost, scheduler cost, backend drain cost, and missed-deadline cost
- no new allocation or logging enters the hot path

Status:

- DONE

Implementation note:

- `SequencerRuntimeService` now separates loop-lane profiling from internal timer profiling:
  - timer callback max/average
  - timer playback max/average
  - backend drain max/average
  - maximum MIDI clock burst
  - MIDI clock burst clamp count
- `SequencerPlaybackService` already exposes scheduler update max, tick jump max, late note count, and note burst counters.
- Dropped note count and queue high-water mark are intentionally deferred to CA-018 because the deadline-aware queue does not exist yet.

---

### CA-016B - Normalize Framework Realtime Time Primitives

Problem:

- the codebase currently has three time mechanisms:
  - `oc::time::millis()` for application time
  - `core::time_compat::micros()` for local core profiling/timing
  - Teensy `HighResolutionClock::micros64()` for hardware-backed microsecond timing
- `SequencerRuntimeService` includes direct Arduino timer/interrupt details
- the upcoming queue needs one explicit deadline arithmetic contract before it can be reviewed safely

Scope:

- `../../open-control/framework/src/oc/time/Time.*`
- `../../open-control/hal-teensy/src/oc/hal/teensy/AppBuilder.hpp`
- `../../open-control/hal-teensy/src/oc/hal/teensy/HighResolutionClock.*`
- `src/config/TimeCompat.hpp`
- `src/sequencer/InternalTransportClock.*`
- `src/sequencer/SequencerRuntimeService.*`

Deliverables:

- framework-level realtime microsecond API, initially one of:
  - `oc::time::micros32()`
  - or a tiny `oc::time::RealtimeClock` helper
- wrap-safe helper for deadline comparisons, for example `signedDeltaUs(nowUs, deadlineUs)`
- native provider based on `std::chrono::steady_clock`
- Teensy provider registered by HAL/AppBuilder
- documentation stating that `oc::time::millis()` is not valid for MIDI deadlines
- minimal migration of core timing reads away from direct Arduino calls where practical
- no broad rewrite of `InternalTransportClock` or MIDI queue in this slice

Non-goals:

- do not introduce a scheduler queue yet
- do not split `IMidi` yet
- do not remove `HighResolutionClock`; keep it as a HAL implementation detail or diagnostics helper
- do not replace every UI/app `millis()` usage

Validation:

- native tests for wrap-safe deadline arithmetic:
  - on time
  - late
  - early
  - 32-bit wrap boundary
- `pio test -e native -f test_MidiClockSyncService`
- `pio test -e native -f test_StandaloneSequencerRuntimeGate`
- `pio run -e dev`

Success criteria:

- CA-018 can express deadlines without using `core::time_compat` or raw Arduino `micros()`
- a reviewer can tell app time and realtime time apart from API names alone
- no new direct `#ifdef ARDUINO` spreads into sequencer domain code
- no mutable 64-bit clock object is required by the queue hot path

Status:

- DONE

Implementation note:

- `open-control/framework` now exposes `oc::time::micros32()`, `oc::time::isMicrosConfigured()`, and `oc::time::signedDeltaUs(...)`.
- Native framework builds default `micros32()` to `std::chrono::steady_clock`.
- Teensy `AppBuilder` registers a HAL-owned microsecond provider.
- Core `TimeCompat` now prefers `oc::time` providers and keeps local fallbacks for test/bootstrap contexts.

---

### CA-017 - Introduce Tick-Domain Scheduled Event Producer

Problem:

- the sequencer engine needs a transport-agnostic event boundary

Scope:

- `../../open-control/note/src/oc/note/sequencer/NoteScheduler.hpp`
- `../../open-control/note/src/oc/note/sequencer/StepSequencerEngine.*`
- new producer/sink type in `open-control/note`
- native tests in `open-control/note` if available, plus `core` integration tests

Deliverables:

- a transport-agnostic event type, for example:

```cpp
struct ScheduledNoteEvent {
    uint32_t tick;
    EventType type;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
};
```

- a sink/collector interface that receives due tick-domain events
- `StepSequencerEngine` can produce events without knowing about `MidiAPI`
- standalone runtime uses an event sink as its output target

Validation:

- sequencer handler/state tests pass
- new test proves event ordering:
  - note off before note on at same tick
  - no lost note off
  - schedule overflow is observable

Success criteria:

- musical engine advances scheduling without physical output
- standalone runtime has a producer boundary before queue integration

Status:

- DONE

Implementation note:

- `StepSequencerEngine` now emits `SequencerEvent` objects through `ISequencerEventSink`.
- `NoteScheduler` no longer dispatches to physical MIDI output.
- Core standalone playback uses `SequencerMidiEventSink`.
- Direct-output interface and adapter were removed.

---

### CA-018 - Add Core Realtime MIDI Event Queue

Problem:

- USB queue exists, but it lacks musical deadlines and late policy
- scheduler needs a non-blocking target with explicit timing semantics

Scope:

- new `src/sequencer/RealtimeMidiEvent.*`
- new `src/sequencer/RealtimeMidiQueue.*`
- `src/sequencer/SequencerPlaybackService.*`
- `src/sequencer/SequencerRuntimeService.*`

Deliverables:

- fixed-capacity queue
- no dynamic allocation
- deadline in absolute microseconds
- explicit late/drop/overflow counters
- note-on/note-off pairing behavior documented

Validation:

- unit tests for queue ordering, overflow, late-send, drop, and note-off priority
- `pio test -e native -f test_SequencerStepHandler`
- `pio test -e native -f test_MidiClockSyncService`

Success criteria:

- scheduler never calls `MidiAPI::sendNoteOn/Off()` directly in the standalone path
- backend congestion becomes visible
- late events do not silently bunch

Status:

- DONE

Implementation note:

- Core now owns `RealtimeMidiEvent` and `RealtimeMidiQueue`.
- The queue is fixed-capacity and deadline-ordered.
- Note-off events have priority at the same deadline.
- When full, the queue may replace a pending note-on with a note-off.
- Large-late note-on events are dropped and counted.
- Overflow, late-send, drop, high-water, and max-drain counters are tracked.
- Native queue tests cover ordering, not-due events, late send, drop, overflow, and note-off survival under pressure.

---

### CA-019 - Migrate Standalone Playback To Producer + Queue

Problem:

- standalone playback sends produced events directly to `MidiAPI`

Scope:

- `src/sequencer/SequencerPlaybackService.*`
- `src/sequencer/SequencerMidiEventSink.*`
- `src/sequencer/SequencerRuntimeService.*`
- `../../open-control/note/src/oc/note/sequencer/StepSequencerEngine.*`

Deliverables:

- `SequencerPlaybackService` collects scheduled events from each track
- tick-domain events are mapped to `deadlineUs`
- mapped events are pushed to `RealtimeMidiQueue`
- standalone runtime stops using the direct event sink once the queue exists

Validation:

- search audit:
  - standalone runtime constructs the realtime queue
  - no standalone scheduler call path reaches `MidiAPI::sendNoteOn/Off()` directly
- `pio test -e native -f test_SequencerStepHandler`
- `pio run -e dev`

Success criteria:

- the only live standalone output path is producer -> realtime queue -> backend drain
- no duplicate direct output path remains

Status:

- DONE

Implementation note:

- `SequencerMidiEventSink` now writes to `RealtimeMidiQueue`.
- `SequencerPlaybackService` sets the tick-to-microsecond timeline before engine updates.
- `SequencerRuntimeService` owns the queue and drains it from the active runtime lane.
- Standalone playback no longer sends note on/off directly through `MidiAPI`.

---

### CA-020 - Split MIDI Backend Input, Output, And Deadline Drain

Problem:

- `UsbMidi::update()` currently both drains output and polls input
- external clock is tied to cooperative `usbMIDI.read()` cadence
- output drain has no event deadline semantics

Scope:

- `../../open-control/framework/src/oc/interface/IMidi.hpp`
- `../../open-control/framework/src/oc/api/MidiAPI.*`
- `../../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.*`
- `../../open-control/hal-teensy/src/oc/hal/teensy/AppBuilder.hpp` if construction changes

Deliverables:

- explicit backend methods or policy split:
  - input poll / capture
  - enqueue output
  - drain output by deadline or budget
- USB drain budget counters
- clear documentation that USB is bounded/measured, not perfect end-to-end realtime

Validation:

- USB output queue tests if available
- `pio run -e dev`
- hardware capture comparing idle vs UI-stress output jitter

Success criteria:

- output drain no longer means "drain everything whenever loop reaches this call"
- input/external clock limitations are explicit and reviewable

Status:

- DONE

Implementation note:

- `IMidi` now exposes explicit `pollInput()` and budgeted `serviceOutput(budgetUs)` methods.
- `MidiAPI` forwards budgeted output drains.
- `OpenControlApp` polls MIDI input before pre-context hooks and drains output with bounded budgets around context work.
- Teensy `UsbMidi` splits input polling from output draining.
- Core runtime drains backend output through the budgeted API.

---

### CA-021 - Define External Clock Ingress Strategy

Problem:

- internal master can be protected with timer/deadline scheduling
- external USB clock remains limited by `usbMIDI.read()` polling in `UsbMidi::update()`

Scope:

- `../../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.*`
- `src/sequencer/MidiClockSyncService.*`
- `src/sequencer/SequencerRuntimeService.*`

Deliverables:

- documented strategy for external clock:
  - current USB limitations
  - timestamp capture point
  - queue or callback path for clock events
  - DIN/UART recommendation if stronger ingress determinism is required
- no ambiguous claim that USB slave mode is perfectly independent of loop cadence until proven

Validation:

- hardware clock slave stress test
- counters for external clock gap/max interval/jitter estimate

Success criteria:

- internal and external clock guarantees are stated separately
- product documentation can honestly describe each mode

Implementation note:

- External clock guarantees are documented in `EXTERNAL_CLOCK_INGRESS_STRATEGY.md`.
- `MidiClockSyncService` records external clock count, max interval, max host gap, and max jitter.
- `SequencerRuntimeService` includes those counters in perf windows when logging is enabled.

Status:

- DONE

---

### CA-022 - Extract Runtime Snapshot Ownership

Problem:

- `SequencerRuntimeService` currently owns snapshot refresh, clock ownership, timer callback, UI projection, profiling, and subscriptions
- this makes the runtime harder to audit as the output queue lands

Scope:

- `src/sequencer/SequencerRuntimeService.*`
- `src/sequencer/SequencerRuntimeStateSync.*`
- `src/sequencer/SequencerRuntimeSnapshotBank.*`

Deliverables:

- named snapshot-bank owner
- explicit commit API
- no direct runtime reads from `CoreState`
- atomic publication to timer/output lanes

Validation:

- `pio test -e native -f test_StandaloneSequencerRuntimeGate`
- targeted runtime snapshot tests
- search audit for direct live state reads in timer path

Success criteria:

- a reviewer can see the exact snapshot boundary
- scheduler/output lanes consume immutable state only

Status:

- DONE

---

### CA-023 - Reduce Sequencer UI Render Churn After Realtime Boundary Lands

Problem:

- UI render work is heavy and visually noisy
- it is currently too easy for runtime visual changes to look like data changes

Scope:

- `src/ui/sequencer/StepGrid.*`
- `src/ui/sequencer/StepGridRenderLogic.*`
- `src/ui/sequencer/StepGridRenderPlanner.*`
- `src/ui/sequencer/SequencerHeaderBar.*`
- `src/ui/sequencer/SequencerHeaderBarRenderModel.*`
- `src/ui/view/SequencerView.*`
- `src/ui/sequencer/SequencerViewModelBuilder.*`

Deliverables:

- header strip no longer subscribes to playhead ticks
- header strip has no playhead/progress render fields
- `probabilityCycleActiveChanged` no longer marks tile data dirty
- playhead remains owned by `StepGrid`
- probability-cycle masking updates only visual opacity/label state

Validation:

- `pio run -e dev`
- targeted sequencer native tests
- scan confirms header playhead references are gone

Success criteria:

- UI feels better
- realtime correctness does not depend on this improvement

Status:

- DONE

---

### CA-024 - Move Core Timer And IRQ Primitives Behind Framework Realtime Owners

Problem:

- core runtime policy was coupled to `IntervalTimer` and local IRQ guard code
- the same critical-section pattern existed in multiple core files

Scope:

- `../../open-control/framework/src/oc/realtime/InterruptGuard.hpp`
- `../../open-control/framework/src/oc/realtime/PeriodicTimer.hpp`
- `src/sequencer/SequencerRuntimeService.*`
- `src/sequencer/SequencerRuntimeSnapshotBank.cpp`

Deliverables:

- one framework interrupt guard
- one framework periodic timer wrapper
- no direct `IntervalTimer` include in core runtime service
- no local core IRQ guard classes

Validation:

- `pio run -e dev`
- targeted realtime native tests
- scan confirms core sequencer has no direct `IntervalTimer`, local IRQ guard, or raw IRQ calls

Success criteria:

- core owns realtime policy
- framework/HAL owns primitive mechanics

Status:

- DONE

---

### CA-025 - Extract Internal Timer Lane From Runtime Service

Problem:

- `SequencerRuntimeService` still carried timer mechanics, transport advancement, clock burst limiting, output drain, and timer profiling
- this made the runtime owner harder to audit

Scope:

- `src/sequencer/SequencerInternalTimerLane.*`
- `src/sequencer/SequencerTiming.hpp`
- `src/sequencer/SequencerRuntimeService.*`

Deliverables:

- timer callback code lives in `SequencerInternalTimerLane`
- runtime service decides whether the timer lane owns transport
- tick period helper has one shared owner
- timer profiling stays with the timer lane

Validation:

- `pio run -e dev`
- targeted realtime native tests
- scan confirms removed runtime timer helper names are gone

Success criteria:

- runtime service reads as orchestration
- timer lane reads as realtime execution

Status:

- DONE

---

## 6. Required Execution Order

Strict order:

1. CA-016
2. CA-016B
3. CA-017
4. CA-018
5. CA-019
6. CA-020
7. CA-021
8. CA-022
9. CA-023
10. CA-024
11. CA-025

Why:

- counters before changes
- realtime time contract before scheduler/queue deadlines
- producer before queue
- queue before runtime migration
- runtime migration before backend split
- backend split before honest external-clock guarantee
- snapshot extraction once the target data flow is clear
- UI cleanup after realtime correctness no longer depends on UI cost
- timer/IRQ primitive cleanup after runtime ownership is stable
- timer lane extraction after primitive ownership is explicit

Exception rule:

- CA-022 may move earlier only if CA-019 becomes too risky without a named snapshot owner.
- CA-016B may be partially implemented inside CA-018 only if it remains a small framework-only prerequisite commit before the queue itself.

---

## 7. Explicitly Unsafe Shortcuts

Do not:

- optimize `StepGrid` first and call the realtime problem solved
- leave direct output and queue output both active in standalone
- add a generic event bus to the hot path
- add logs per MIDI event
- allocate per note, per tick, or per timer callback
- compute MIDI deadlines from `oc::time::millis()`
- compare absolute microsecond timestamps with raw `<` or `>` across possible wrap boundaries
- spread direct Arduino `micros()`, `IntervalTimer`, or IRQ masking into new domain modules
- claim USB MIDI has perfect end-to-end timing without measurement
- move UI/domain work into the internal timer lane to hide loop-coupling
- let the scheduler read `CoreState` or UI state directly

---

## 8. Merge Gates

Every implementation PR in this stack must include:

- code search evidence for removed ambiguous paths
- targeted tests
- `pio run -e dev` unless the change is docs-only
- updated docs if a contract changes
- counters or hardware capture if timing behavior is claimed to improve

For non-doc PRs, the PR description must answer:

- What is the authoritative path after this change?
- What direct/parallel path was removed?
- What cannot happen anymore?
- What remains intentionally unsolved?

---

## 9. Completed Implementation Slices

Completed slices:

- CA-016: realtime counters
- CA-016B: framework realtime microsecond primitive
- CA-017: tick-domain sequencer events
- CA-018: core realtime MIDI queue
- CA-019: standalone playback queue migration
- CA-020: backend input/output split and budgeted drain
- CA-021: external clock ingress strategy
- CA-022: runtime snapshot ownership
- CA-023: reduce sequencer UI render churn
- CA-024: framework realtime timer/IRQ primitives
- CA-025: internal timer lane extraction

Next slice:

- hardware timing validation under UI stress

---

## 10. Definition Of Done For The Whole Stack

The realtime refactor stack is done only when:

- standalone scheduler no longer dispatches directly to `MidiAPI`
- app time and realtime time have separate APIs and reviewable ownership
- outgoing MIDI uses a deadline-aware queue
- late/drop/overflow behavior is explicit and measured
- internal clock phase is independent from UI load
- external clock limitations and guarantees are stated honestly
- runtime consumes immutable snapshots
- no duplicate standalone output path remains
- UI render improvements are treated as UX/headroom, not as the timing guarantee
