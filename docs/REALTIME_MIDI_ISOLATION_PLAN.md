# Realtime MIDI Isolation Plan

> Status: working memory
> Scope: standalone firmware in this repository
> Goal: ensure outgoing MIDI timing stays stable under UI overload, view switches, and overlay churn

## Problem Statement

Today the standalone sequencer runtime already has a meaningful split:

- the authoritative runtime owner lives in `main.cpp`
- the runtime is updated from the app pre-context hook
- internal master transport progression runs from `IntervalTimer`

That is already better than the old fully cooperative loop model.

However, the current contract is still only partly explicit:

- internal master still fuses clock, scheduler, and backend drain in one timer callback
- external/fallback paths still depend on cooperative loop cadence
- future reviewers could easily add work to the wrong lane because the rules are not stated clearly

Without an explicit contract, the next refactor could re-introduce exactly the coupling we just removed.

This would create:
- note jitter
- late note emission
- bursty catch-up after a stall
- visible phase jumps during heavy view transitions

The target is simple:

- UI may drop frames
- MIDI timing may not drift, jump, or bunch

## Current Accepted Contract (2026-04-16)

This section is normative for the current working implementation after `CA-010`.

### Authoritative owner and entry point

- `main.cpp` owns the single standalone `SequencerRuntimeService` instance in PSRAM
- `OpenControlApp::registerPreContextUpdateHook(...)` is the only live standalone runtime update entry point
- `StandaloneContext::update()` is not allowed to tick the sequencer runtime

### Current lane split

1. Control lane
   - entry point: `SequencerRuntimeService::update()`
   - runs from the app pre-context hook before context/UI work
   - captures runtime config and track-bank snapshot
   - decides whether the internal timer owns transport
   - publishes immutable inputs to the timer lane when internal master is active
   - publishes UI/state projections outside the timer callback

2. Timer lane (`internal master` only)
   - entry point: `SequencerRuntimeService::onInternalTimer_()`
   - advances the internal transport clock
   - emits MIDI realtime `start` / `stop` / `clock`
   - runs `sequencer_playback_.update(...)`
   - drains `midi_.serviceOutput()`

3. Projection lane
   - still runs on the control lane today
   - `publishPlaybackUiFromTimerPath_()` snapshots timer-lane output under lock
   - `SequencerPlaybackService::publishUiProjection(...)` applies status-bar pulses outside the timer callback
   - `MidiClockSyncService::publishUiState(...)` applies sync/tempo/transport projections outside the timer callback

4. External/fallback path
   - when the internal timer does not own transport, `SequencerRuntimeService::update()` remains the active driver
   - this path is still loop-coupled and is tolerated for now
   - any larger redesign here must pass the measurement gate below

### Timer-lane classification (`SequencerRuntimeService::onInternalTimer_()`)

Required now:

- read only the committed runtime snapshot and committed clock config
- advance the internal transport clock deterministically
- emit outgoing MIDI realtime clock/start/stop for the internal-master path
- run playback scheduling against the committed snapshot
- drain the backend output path because the current runtime still relies on that coupling

Deferrable later:

- splitting backend drain into a dedicated fixed-capacity output lane
- moving realtime clock emission behind the same output abstraction
- emitting richer timer-lane telemetry if it can be done without expanding the callback budget

Forbidden in future additions:

- LVGL calls or view lifecycle work
- direct `StatusBarState` / `MidiSyncState` writes
- event bus publication, persistence, or storage I/O
- dynamic allocation, container growth, or logging
- reading mutable live state directly instead of consuming the committed snapshot/config

### Measurement gate for future scheduler/output split

Do not split the current clock/scheduler/output path further in this wave unless at least one of these becomes true:

- `[Perf][SequencerRuntime]` shows sustained `maxClockUs >= 1000` or `maxPlaybackUs >= 1000` under the real stress scenarios
- `[Perf][SequencerPlayback]` shows `tickJumpMax > 1`, growing `lateNotes`, or `midiSendMaxUs` spikes that correlate with audible timing issues
- loopback or DAW capture shows bunching/jitter that cannot be explained by visual lag alone

If none of these are true, prefer explicit contracts, review guardrails, and narrow cleanups over speculative queue architecture churn.

## Current Coupling Points

Relevant code paths today:

- `main.cpp`
  - owns the authoritative standalone runtime in PSRAM
  - the app pre-context hook is the only standalone runtime entry point
- `src/sequencer/SequencerRuntimeService.cpp`
  - control lane runs from the app pre-context hook
  - internal master clock + playback + backend drain still share one timer callback
- `src/sequencer/SequencerPlaybackService.hpp`
  - note activity is collected during playback update, then published as status-bar projections later
- `../open-control/note/src/oc/note/sequencer/StepSequencerEngine.cpp`
  - late runtime updates are caught up by `processUntil(...)`, which avoids silent loss but can bunch events

## Root Cause

The remaining problem is not "LVGL sends MIDI".

The remaining problem is:

- the internal-master timer lane still owns too many realtime responsibilities at once
- the external/fallback path still depends on cooperative loop cadence
- the exact lane contract was not explicit enough to prevent accidental recoupling

## Target Architecture

We want 4 clearly separated lanes:

1. Clock lane
   - owns musical time
   - driven by hardware timer or equally deterministic high-priority source

2. Scheduler lane
   - converts clock ticks into timestamped note events
   - never performs UI work

3. MIDI output lane
   - drains timestamped events to the backend
   - must be non-blocking from the scheduler point of view

4. UI lane
   - renders projections of already-committed state
   - may lag visually without perturbing the 3 lanes above

## Required Invariants

The design is only acceptable if these remain true:

- Musical time is derived from an absolute monotonic clock, not from loop cadence.
- A long LVGL frame cannot delay the next sequencer tick.
- The realtime path never calls LVGL directly.
- The realtime path never emits status/UI pulses inline.
- Sequencer edits coming from UI are committed atomically at a safe boundary.
- MIDI send operations visible to the scheduler are non-blocking.
- Late events are measured explicitly and never hidden.

## Phase Plan

### Phase 0 - Measurement First

Add explicit runtime counters before large refactors:

- max scheduler lateness in us
- number of late note events
- number of dropped note events
- queue high-water mark
- longest MIDI backend drain time
- longest UI frame time

Why first:
- we need proof after each phase
- we need to know whether the remaining issue is clock, scheduling, backend blocking, or UI coupling

Acceptance for phase 0:
- we can observe timing stress during playback + aggressive view switching
- we can distinguish "late but sent", "dropped", and "on time"

### Phase 1 - Remove UI Side Effects From The Realtime Path

The outgoing note path must stop mutating UI-facing state inline.

Current anti-pattern:
- note emission also updates status bar pulse signals

Replace with:
- lightweight telemetry event publication
- UI consumes telemetry asynchronously on its own path

Result:
- the MIDI path becomes one-way: clock -> scheduler -> MIDI

Acceptance for phase 1:
- note send path contains no LVGL calls
- note send path contains no status-bar mutation

### Phase 2 - Move Clock Ownership Out Of The Main Loop

Internal transport must no longer depend on cooperative loop cadence.

Preferred shape:
- hardware timer or equivalent high-priority periodic callback
- absolute deadline tracking in microseconds
- `next_tick_us += interval_us`, never `next_tick_us = now + interval`

For external MIDI clock:
- capture incoming clock timestamps as close as possible to ingress
- update transport phase from timestamped input, not from delayed UI-side polling

Acceptance for phase 2:
- a long UI frame does not shift the internal musical clock
- source switch and lock/unlock do not reset phase unexpectedly

### Phase 3 - Introduce A Timestamped MIDI Event Queue

The scheduler should enqueue timestamped events.
The MIDI output lane should drain that queue independently.

Properties of the queue:
- fixed capacity
- no dynamic allocation
- no locking on the hot path if avoidable
- explicit overflow policy

Recommended overflow policy:
- never silently bunch old events
- count missed deadlines
- prefer controlled drop/resync over audible flam bursts

Acceptance for phase 3:
- scheduler never blocks on backend send
- backend congestion is visible in counters

### Phase 4 - Make Sequencer State Consumption Atomic

UI-originated edits must not be read piecemeal by the realtime path.

Introduce a commit model:
- UI edits a staging model or mutable state
- runtime consumes immutable snapshots
- commit happens at a safe musical boundary

Safe boundaries may differ by feature:
- immediate at next tick
- quantized at next step
- quantized at next page/cycle for larger structural changes

Use this especially for:
- note / velocity / gate edits
- enabled-mask changes
- range operations
- length / division changes

Acceptance for phase 4:
- no mixed-state reads inside one musical decision
- structural edits cannot cause transient malformed scheduling

### Phase 5 - Define A Late Event Policy

No realtime system is complete without explicit late-event semantics.

Decide and document:
- how much lateness is still acceptable
- when a note event should be sent immediately
- when it should be dropped
- when transport should resync instead

Practical rule:
- small lateness: send and count
- large lateness: drop and count
- repeated lateness: trigger resync strategy

Goal:
- avoid audible bursts of stale events after a UI stall

### Phase 6 - Stress Validation

Validate under the exact scenarios that previously hurt:

- playback with 8 tracks
- repeated view selector open/close
- sequencer <-> macro switches
- overlay churn during playback
- dense note patterns
- internal clock and external clock modes

Validation methods:
- MIDI loopback capture in DAW or monitor
- compare timestamp deltas idle vs stress
- verify note order, note-off pairing, and jitter envelope

Acceptance for phase 6:
- no missing or reordered note on/off under stress
- no measurable phase jump after repeated UI stress
- jitter stays within defined envelope for the chosen backend

## Feasibility Matrix

This matrix maps the desired outcome to the current repository structure and the likely implementation shape.

| Objective | Feasibility | Current location | What is missing | Main risk |
| --- | --- | --- | --- | --- |
| Make internal musical time independent from UI load | High | `src/sequencer/MidiClockSyncService.cpp`, `../open-control/note/src/oc/note/clock/InternalClock.cpp` | Replace loop-driven internal clock ownership with PIT/GPT or `IntervalTimer` based timekeeping | Incorrect phase handoff during start/stop or clock source switch |
| Keep scheduler decisions stable under LVGL spikes | High | `src/sequencer/SequencerRuntimeService.cpp`, `../open-control/note/src/oc/note/sequencer/StepSequencerEngine.cpp` | Separate scheduler cadence from cooperative loop cadence | Catch-up logic may still bunch stale events if late-event policy is not enforced |
| Remove all UI work from note emission | Very high | `src/sequencer/SequencerPlaybackService.hpp` | Replace inline status-bar pulses with async telemetry | Low technical risk, but easy to miss secondary UI side effects |
| Preserve coherent sequencer state while UI edits occur | High | `src/state/`, `src/sequencer/`, `../open-control/note/src/oc/note/sequencer/` | Snapshot or staged commit model at a safe musical boundary | Mixed reads during transition if partial migration leaves shared mutable state |
| Make outbound MIDI non-blocking from scheduler point of view | Medium-high | `../open-control/framework/src/oc/interface/IMidi.hpp`, `../open-control/framework/src/oc/api/MidiAPI.cpp`, `../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.cpp` | Fixed-capacity timestamped event queue plus a dedicated drain path | Backend semantics may still block or batch in ways hidden from the scheduler |
| Guarantee tight timing on DIN/UART output | High | No Teensy DIN backend currently wired in `hal-teensy` | Add Teensy serial MIDI transport in HAL and route sequencer output through it | Requires a new HAL brique, but the transport model is much more deterministic |
| Guarantee tight timing on USB MIDI output | Medium | `../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.cpp`, Teensy core `usb_midi.h` | Queue-based output plus measured backend drain and explicit late policy | USB host scheduling and buffering place an upper bound on determinism |
| Timestamp external MIDI clock close to ingress | Medium | `../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.cpp`, `src/sequencer/MidiClockSyncService.cpp` | Better ingress timestamping path than cooperative `usbMIDI.read()` polling | Slave timing remains limited by USB poll/read cadence unless ingress path changes |
| Prove timing quality under stress | High | No dedicated instrumentation yet | Add lateness, drop, backlog, drain-time and jitter counters | Without counters, regressions remain anecdotal and hard to review objectively |

## Repository Map For The Rework

These are the concrete areas to touch if the project moves forward:

- `src/sequencer/`
  - move clock ownership out of the loop
  - split runtime, scheduler, and telemetry responsibilities more clearly
- `src/state/`
  - introduce safe commit boundaries or immutable snapshots for runtime consumption
- `../open-control/note/src/oc/note/`
  - keep sequencing logic pure and unaware of UI/runtime presentation concerns
  - add explicit late-event semantics where needed
- `../open-control/framework/src/oc/interface/IMidi.hpp`
  - likely remains the stable abstraction boundary
- `../open-control/hal-teensy/src/oc/hal/teensy/`
  - add the realtime-facing backend support actually needed on hardware
  - this is the natural place for a timer-backed or queue-backed transport helper
- Teensy core
  - `IntervalTimer.h` is the simplest supported entry point for a hardware-timer clock lane
  - `imxrt.h` exposes PIT and GPT directly if lower-level control is needed

## Recommended Technical Direction

If the project wants the best ratio of robustness to implementation risk, the clean direction is:

1. Keep sequencing logic in `open-control/note`.
2. Add a small realtime transport/scheduler support layer in `open-control/hal-teensy`.
3. Keep UI and status telemetry entirely outside the hot note path in `midi-studio/core`.
4. Treat USB MIDI as "measured and bounded" rather than "hard realtime guaranteed".
5. Add a DIN/UART backend if the product goal requires the strongest timing guarantee.

## 3-PR Implementation Roadmap

This roadmap is intentionally narrow.
It aims to improve timing isolation quickly without forcing a large architectural rewrite up front.

### PR 1 - Measure And Decouple The Hot Path

Goal:
- make timing failures visible
- remove obvious non-realtime side effects from note emission

Scope:
- add counters for:
  - scheduler lateness
  - late events sent
  - dropped events
  - queue/backlog high-water mark
  - longest backend drain
  - longest UI frame
- remove inline UI/status mutations from the note send path
- replace them with async telemetry or deferred status updates

Touch points:
- `src/sequencer/SequencerPlaybackService.hpp`
- `src/sequencer/`
- `src/state/` or another non-realtime telemetry sink
- optional lightweight debug reporting path

Do not do in this PR:
- no hardware timer migration yet
- no queue redesign yet
- no backend abstraction expansion yet

Acceptance:
- note send path does not mutate UI-facing state
- stress playback now reports hard numbers instead of anecdotal behavior
- no behavioral regression in normal playback

Why first:
- lowest risk
- gives an objective baseline
- removes one current architectural violation immediately

### PR 2 - Isolate Clock Ownership And Scheduler Cadence

Goal:
- stop deriving musical time from cooperative loop cadence

Scope:
- introduce a dedicated timebase for internal clocking using `IntervalTimer` first
- keep the implementation small and explicit
- convert internal transport progression to absolute microsecond deadlines
- keep scheduler decisions driven from that dedicated timebase, not from UI-loop cadence

Preferred implementation shape:
- timer callback owns clock progression only
- timer callback does not touch LVGL
- timer callback does not do heavy MIDI backend work
- scheduler-facing state update is minimal and deterministic

Touch points:
- `src/sequencer/MidiClockSyncService.cpp`
- `src/sequencer/SequencerRuntimeService.cpp`
- `../open-control/note/src/oc/note/clock/`
- new helper in `../open-control/hal-teensy/src/oc/hal/teensy/` if needed

Do not do in this PR:
- no full outbound MIDI queue yet unless required by the timer split
- no DIN backend yet

Acceptance:
- internal clock phase is stable under aggressive UI stress
- repeated overlay/view churn no longer shifts transport phase
- counters show lower scheduler lateness under the same stress test

Why second:
- this attacks the root cause directly
- it separates "musical time" from "UI frame budget"

### PR 3 - Queue Outbound MIDI And Define Backend Guarantees

Goal:
- make note emission non-blocking from the scheduler point of view
- formalize what is guaranteed on USB and what requires DIN/UART

Scope:
- add a fixed-capacity timestamped MIDI event queue
- drain output independently from the scheduler
- define and implement a late-event policy
- measure backend drain time and queue pressure
- if product goals require stronger guarantees, add a Teensy DIN/UART backend in `hal-teensy`

Preferred implementation order inside this PR:
1. queue + late-event policy on the current USB backend
2. benchmark and characterize real behavior
3. decide whether DIN/UART support is required for product-level guarantees

Touch points:
- `../open-control/framework/src/oc/interface/IMidi.hpp`
- `../open-control/framework/src/oc/api/MidiAPI.cpp`
- `../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.cpp`
- new queue/helper files
- optional new Teensy serial MIDI backend in `hal-teensy`

Acceptance:
- scheduler no longer blocks on backend send semantics
- queue overflow and late events are explicit and counted
- team can state clearly:
  - what USB guarantees
  - what USB does not guarantee
  - whether DIN/UART is required for the strongest timing target

Why third:
- queueing and backend semantics are easiest to reason about once clock ownership is already fixed
- it avoids mixing clock refactor, queue refactor, and transport expansion in one risky change

## Recommended Sequencing Rule

Do not merge PR 2 before PR 1 counters exist.
Do not merge PR 3 before PR 2 demonstrates clock stability improvement.

That order keeps each PR reviewable and makes regressions attributable.

## Minimal Success Path

If the team wants the smallest path that still materially improves reliability:

1. ship PR 1
2. ship PR 2
3. evaluate whether PR 3 needs only USB queueing or both USB queueing and DIN/UART support

That is the lowest-overhead route which still respects the realtime goal.

## PR 1 Execution Skeleton

This is the concrete implementation brief for the first PR.

### PR 1 Title

`core: instrument sequencer timing and remove UI side effects from note emission`

### PR 1 Objectives

- make realtime timing failures measurable
- ensure note emission no longer mutates UI-facing state inline
- keep behavior unchanged for normal playback and visible status feedback

### PR 1 Exact Scope

In scope:

- add internal counters for:
  - scheduler lateness
  - late note events sent
  - dropped note events
  - longest backend send/drain duration
  - queue high-water mark if a temporary queue is introduced
  - longest UI frame time
- move status-bar pulse side effects out of `sendNoteOn`
- publish lightweight note telemetry instead
- let UI consume that telemetry asynchronously on its own update path

Out of scope:

- no hardware timer ownership yet
- no transport queue redesign as a required part of this PR
- no DIN/UART backend work
- no broad state architecture rewrite

### Most Likely Files

Primary files:

- `src/sequencer/SequencerPlaybackService.hpp`
- `src/sequencer/SequencerRuntimeService.cpp`
- `src/sequencer/`
- `src/state/`
- `src/ui/` or status-bar related files only if needed to consume deferred telemetry

Potential support files:

- a small telemetry struct/header under `src/sequencer/` or `src/state/`
- an existing debug/perf reporting path, if one already exists and can accept the new counters cleanly

### Expected Code Shape

Preferred shape:

- `SequencerPlaybackService` only emits musical events and lightweight telemetry
- telemetry is plain data, not UI code
- status-bar updates happen later from state/runtime/UI code already allowed to be non-realtime
- counters are monotonic and cheap to update

Avoid:

- adding LVGL calls anywhere near the note emission path
- adding logs on every note event
- introducing allocations in the hot path
- building a generic event bus if a small dedicated telemetry channel is enough

### Review Checklist

Reviewers should verify:

- `sendNoteOn` no longer mutates `StatusBar` directly
- no new LVGL or widget dependencies entered the sequencer hot path
- no allocation was introduced per note event
- counters are cheap, bounded, and easy to disable or ignore in production
- the replacement telemetry path cannot block note emission
- note-on / note-off semantics are unchanged

### Test Plan

Functional tests:

- playback still emits the same notes under idle conditions
- status bar still shows note activity
- no duplicate note pulses appear
- no missing note-off regressions appear

Stress tests:

- 8-track playback while switching views repeatedly
- open/close overlays while playback runs
- sequencer idle vs sequencer under UI stress comparison

Verification targets:

- counters increase when stress is real
- no obvious behavioral regression in note output
- visible UI feedback remains present but no longer sits on the hot path

### Exit Criteria

PR 1 is done when all of the following are true:

- realtime note emission is free of UI side effects
- timing counters exist and can be observed during stress
- normal playback behavior matches the pre-PR baseline
- the team has enough data to decide whether PR 2 reduced actual lateness

### Main Review Risks

- hidden UI coupling elsewhere in the send path beyond the status bar
- accidental behavior change in note activity indicators
- instrumentation becoming too invasive or too noisy
- reviewers conflating measurement changes with timing fixes that belong to PR 2

### Non-Negotiable Constraint

If a proposed PR 1 change improves visibility but adds work to the hot path, reject it.

PR 1 must make the system more observable without making realtime behavior worse.

## PR 2 Execution Skeleton

This is the concrete implementation brief for the second PR.

### PR 2 Title

`core: isolate internal transport timing from cooperative UI cadence`

### PR 2 Objectives

- move internal musical time ownership out of the cooperative update cadence
- keep scheduler cadence stable under LVGL spikes
- preserve existing musical behavior while reducing timing sensitivity to UI load

### PR 2 Exact Scope

In scope:

- introduce a dedicated high-priority internal transport timebase
- base internal tick progression on absolute microsecond deadlines
- keep the timer callback minimal and deterministic
- make the sequencer runtime consume that timebase rather than deriving timing from the loop pace
- measure whether scheduler lateness improves under the same UI stress scenarios

Out of scope:

- no full outbound MIDI queue yet unless required to preserve correctness
- no DIN/UART backend work yet
- no large snapshot/state-commit refactor unless strictly needed by the clock split
- no attempt to "fix USB guarantees" in this PR

### Preferred Technical Shape

Use the smallest viable hardware-timer abstraction first:

- prefer `IntervalTimer` as the first implementation vehicle
- keep direct PIT/GPT register work as a second step only if `IntervalTimer` proves too limiting
- track phase with absolute deadlines:
  - `next_tick_us += interval_us`
  - avoid `next_tick_us = now + interval_us`

The timer side should:

- own transport cadence
- avoid LVGL
- avoid logs
- avoid allocations
- avoid heavy backend I/O

The cooperative loop side should:

- observe transport state
- perform non-realtime follow-up work
- remain tolerant to visual overload without moving the musical phase

### Most Likely Files

Primary files:

- `src/sequencer/MidiClockSyncService.cpp`
- `src/sequencer/SequencerRuntimeService.cpp`
- `../open-control/note/src/oc/note/clock/InternalClock.cpp`

Likely support files:

- new helper under `../open-control/hal-teensy/src/oc/hal/teensy/`
- possibly a small interface/header to isolate timebase ownership cleanly

Reference hardware locations:

- Teensy core `IntervalTimer.h`
- Teensy core `imxrt.h` for PIT/GPT if lower-level control becomes necessary

### Architectural Constraints

PR 2 should preserve these rules:

- the timer callback is not the new place where UI or MIDI backend work happens
- one source of truth owns internal transport phase
- source switching remains explicit and reviewable
- external clock behavior is not silently changed while refactoring internal clocking

### Review Checklist

Reviewers should verify:

- internal clock progression no longer depends on cooperative loop cadence
- timer code is minimal and bounded
- no UI code or status telemetry leaks into the timer path
- no hidden blocking path is introduced in the timer callback
- start/stop/reset behavior remains coherent
- source switching between internal and external clock remains understandable

### Test Plan

Functional tests:

- internal clock playback still runs at the same musical tempo
- start, stop, continue, and reset behavior stay correct
- loop/playhead behavior remains consistent

Stress tests:

- repeat the previous heavy UI scenarios while on internal clock
- compare scheduler lateness counters before and after PR 2
- verify that visible UI stalls no longer imply transport phase shifts

Verification targets:

- lower max lateness under the same stress
- no audible bunching introduced by the timer split itself
- no regressions in steady-state playback

### Exit Criteria

PR 2 is done when all of the following are true:

- internal transport cadence is no longer loop-paced
- UI stress no longer shifts internal musical phase in the same way
- the timer path remains small and auditable
- PR 1 counters show measurable improvement or, at minimum, expose the remaining bottleneck clearly

### Main Review Risks

- moving too much logic into the timer callback
- introducing race conditions around start/stop/source-switch state
- keeping both the old and new clock paths alive in a confusing partial migration
- mistaking lower jitter in one mode for a full solution when backend send semantics still remain unchanged

### Non-Negotiable Constraint

If PR 2 solves timing by pushing scheduler, UI, or backend work into the timer callback, reject it.

PR 2 must isolate time ownership, not hide more work in a higher-priority context.

## Recommended First Slice

If we want the highest return with the lowest risk, do this first:

1. Phase 0 measurement
2. Phase 1 remove UI/status mutations from the note path
3. Phase 2 clock ownership isolation

Why this order:
- it attacks the root cause
- it gives immediate visibility on progress
- it avoids optimizing LVGL forever while the musical clock still shares the same failure domain

## Explicit Non-Goals

This plan is not about:
- making every UI switch visually perfect first
- reducing all LVGL spikes before isolating the clock
- abstracting the whole runtime into a generic framework

The priority is musical correctness first, UI smoothness second.

## Success Criteria

We can consider the problem solved when all of the following are true:

- UI overload no longer changes musical phase
- note timing does not bunch after view switches
- external clock lock does not drift under UI stress
- note send path is free of UI-side state mutation
- timing regressions are observable through counters and stress tests

## Decision Rule

Whenever UI smoothness and realtime safety conflict:

- drop UI work
- keep musical time

That rule should remain invariant across future refactors.
