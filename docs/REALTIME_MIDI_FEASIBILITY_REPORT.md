# Realtime MIDI Isolation Feasibility Report

> Status: working architecture report
> Scope: standalone firmware in this repository
> Goal: define a clean, reliable, durable path to make engine timing and outgoing MIDI authoritative under UI overload

## Executive Summary

This refactor is feasible.

The important nuance is where the guarantee can be made:

- **Device-side guarantee**: high confidence, feasible
- **DIN / UART end-to-end guarantee**: high confidence, feasible after adding a Teensy serial MIDI HAL backend
- **USB MIDI end-to-end guarantee**: bounded and measurable, but not absolutely perfect on the host side because USB packetization and shared bandwidth remain external constraints

The codebase already contains several pieces we can build on:

- a Teensy monotonic microsecond source in the HAL
- a fixed-capacity note scheduler in the engine
- timestamped external clock ingress
- targeted profiling that now clearly identifies UI as the dominant remaining source of timing interference

The main conclusion is:

- **UI optimization is necessary, but it is not the core guarantee mechanism**
- the core guarantee mechanism must be **architectural separation** between:
  - clock
  - scheduler
  - MIDI output
  - UI

If we do not remove the shared failure domain between UI work and realtime work, no amount of widget optimization will make the timing contract durable.

## Target Contract

This report assumes the following contract is non-negotiable:

- musical time is authoritative
- scheduler decisions are authoritative
- outgoing MIDI timing is authoritative
- UI is secondary and may lag or drop frames
- UI overload may never alter phase, timing, ordering, or burst behavior of engine/MIDI

In engineering terms, the design is acceptable only if all of the following remain true:

- musical time is derived from an absolute monotonic source, not loop cadence
- a long UI frame cannot delay the next musical deadline
- the realtime path never calls LVGL
- the realtime path never mutates UI state inline
- state consumed by the realtime path is coherent for the duration of one scheduling decision
- backend congestion is measured explicitly
- late events are counted and handled by policy, never hidden

## Current Architecture Audit

### What is already in good shape

The current codebase is not starting from zero.

On Teensy builds, the internal transport is now backed by a monotonic HAL clock rather than a loop-driven millisecond accumulator:

- [InternalTransportClock.hpp](../src/sequencer/InternalTransportClock.hpp)
- [InternalTransportClock.cpp](../src/sequencer/InternalTransportClock.cpp)
- [HighResolutionClock.hpp](../../open-control/hal-teensy/src/oc/hal/teensy/HighResolutionClock.hpp)

The current implementation is especially important here:

- [`InternalTransportClock::update()` on Teensy is a no-op](../src/sequencer/InternalTransportClock.cpp)
- [`tick()` derives phase directly from `clock_.micros64()`](../src/sequencer/InternalTransportClock.cpp)

This means the **time base itself** is already much cleaner than before.

Also promising:

- external MIDI clock ingress is timestamped close to transport ingress in [UsbMidi.cpp](../../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.cpp)
- the engine already uses a fixed-capacity scheduler in [NoteScheduler.hpp](../../open-control/note/src/oc/note/sequencer/NoteScheduler.hpp)
- events are already represented in tick space before dispatch

These are strong building blocks for a durable refactor.

### Where the coupling still exists

The runtime is still serviced by the same cooperative loop that also pays for UI and device work:

- [main.cpp](../main.cpp)
- [OpenControlApp.cpp](../../open-control/framework/src/oc/app/OpenControlApp.cpp)
- [SequencerRuntimeService.cpp](../src/sequencer/SequencerRuntimeService.cpp)

Current order of operations still matters:

1. `OpenControlApp::update()` runs device-facing updates including `midi_->update()`
2. pre-context hooks run the sequencer runtime
3. context / UI update work runs
4. LVGL refresh runs after that in the firmware main loop

That means the time source may be monotonic, but **the servicing cadence of the scheduler and output path is still loop-coupled**.

This is why large UI bursts still show up as:

- `lateNotes > 0`
- `tickJumpMax > 1`
- reduced playback update count during stress
- visible burst/catch-up behavior

### What the profiling proved

Recent profiling already gives a strong answer:

- track switch state copy and handler cost are tiny
- playback handoff on track switch is tiny
- the first post-switch UI render is the real hotspot

Representative measurements from the profiling session:

- track switch state / handler / playback path: roughly `50-130us`
- first track-switch render: roughly `52-56ms`
- `StepGrid` inside that render: roughly `33-35ms`
- repeated non-switch `StepGrid` renders during playback: often `10-26ms`

This is the key architectural takeaway:

- the problem is no longer "track switching logic is slow"
- the problem is "UI render spikes still share the same failure domain as engine servicing"

### The grid is currently too chatty

The current grid diff logic still promotes some runtime visual changes into data-level work:

- [StepGridRenderLogic.cpp](../src/ui/sequencer/StepGridRenderLogic.cpp)
- [StepGridFrameLogic.cpp](../src/ui/sequencer/StepGridFrameLogic.cpp)

In particular, `probabilityCycleActiveChanged` currently contributes to `dataChanged`, which increases tile redraw cost even when the underlying musical content has not structurally changed.

This is a major UI optimization target, but again:

- it is an optimization target
- it is not the root guarantee mechanism

## Backend And Hardware Constraints

### Teensy timing primitives

PJRC documents two timing tools that matter here:

- the general timing table says the ARM cycle counter gives the highest precision, but only for short time spans
- `IntervalTimer` gives precise periodic callbacks, but runs in interrupt context and requires careful sharing discipline

Sources:

- [PJRC Timing Functions](https://www.pjrc.com/teensy/td_timing.html)
- [PJRC IntervalTimer](https://www.pjrc.com/teensy/td_timing_IntervalTimer.html)

This matches the current code direction:

- HAL exposes the raw monotonic source
- domain logic stays in `core`

That split is cleaner long-term than pushing musical transport logic into a HAL timer callback.

### USB MIDI constraints

Current Teensy USB MIDI behavior matters a lot:

- PJRC states USB MIDI messages are grouped into USB packets
- messages may be held briefly, up to 1 ms, to facilitate grouping
- `send_now()` can flush buffered USB MIDI output
- USB bandwidth is shared across devices
- receive callbacks are only called when `read()` is actually invoked

Source:

- [PJRC USB MIDI](https://www.pjrc.com/teensy/td_midi.html)

This has two implications:

1. We can still make the **firmware-side schedule** deterministic.
2. We cannot honestly promise "perfect host-visible zero-jitter USB timing" as a pure firmware property.

So the right target wording is:

- no firmware-induced drift, phase jump, or burst behavior
- remaining USB transport behavior must be bounded, measured, and documented

### DIN / UART feasibility

PJRC also documents standard serial MIDI on hardware UARTs at 31250 baud, separate from USB MIDI:

- [PJRC MIDI Library / Hardware Serial MIDI](https://www.pjrc.com/teensy/td_libs_MIDI.html)

This is important because the current Teensy HAL only wires USB MIDI:

- [AppBuilder.hpp](../../open-control/hal-teensy/src/oc/hal/teensy/AppBuilder.hpp)

There is currently **no dedicated Teensy DIN/UART MIDI backend** in `hal-teensy`.

That means:

- DIN is technically very feasible
- but it requires new HAL work before we can claim strong end-to-end realtime guarantees on that transport

## Feasibility Assessment By Work Item

| Work item | Feasibility | Why | Main risk |
| --- | --- | --- | --- |
| Keep device-side musical phase independent from UI | High | Monotonic HAL time already exists | phase handoff bugs at play/start/stop |
| Stop scheduler catch-up bursts under UI stalls | Medium-High | engine already stores future events in tick space | wrong deadline conversion or insufficient lookahead |
| Add fixed-capacity timestamped MIDI queue | High | `NoteScheduler` already proves fixed-capacity event discipline fits the codebase | overflow policy and resync semantics must be explicit |
| Keep realtime path free of LVGL/UI work | High | architecture already separates most rendering from handlers | hidden state mutations or telemetry leakage |
| Make state consumption atomic for the runtime | Medium | state model is broad and reactive | partial snapshot boundaries and invalidation complexity |
| Add Teensy DIN/UART backend | High | hardware and PJRC support are straightforward | interface design and testing scope |
| Guarantee USB transport with zero host-side jitter | Low | USB itself is packetized and shared | impossible promise rather than implementation failure |
| Reduce UI compute enough to preserve UX | High | profiling clearly identifies hotspots | broad invalidation semantics in sequencer view/grid |

## Recommended Architecture

The durable target is a 4-lane architecture:

### 1. Clock lane

Responsibility:

- own musical time
- maintain absolute phase
- never depend on UI cadence

Recommended implementation:

- keep the raw monotonic source in HAL
- keep transport math in `core`
- define deadlines in `uint64_t us`
- advance by absolute deadlines, not by "now + delta"

### 2. Scheduler lane

Responsibility:

- transform transport time and snapshots into future note events
- operate on immutable or effectively immutable state for one scheduling slice

Recommended implementation:

- evolve the existing engine/scheduler so that it produces timestamped MIDI events ahead of time
- do not dispatch directly to the transport from the scheduler

Important non-goal:

- do **not** put the full musical scheduler into a heavy ISR

The scheduler can stay in normal code as long as it is working ahead of the deadlines it is producing.

### 3. MIDI output lane

Responsibility:

- drain timestamped events to the backend
- make lateness explicit
- avoid blocking the scheduler

Recommended implementation:

- fixed-capacity queue
- transport-specific drain policy
- explicit overflow and lateness counters

Transport split:

- USB: bounded and measured, optional flush policy
- DIN/UART: stronger determinism once the backend exists

### 4. UI lane

Responsibility:

- render projections
- react to already-committed state
- never hold the clock/scheduler/output path hostage

Recommended implementation:

- continue profiling and reducing broad invalidation
- make the grid differentiate structural data changes from runtime-only visual changes
- split expensive track-switch renders across frames if needed

## Detailed Implementation Trajectory

## Stage 0 - Freeze The Contract And Metrics

Objective:

- agree on the contract before changing more code
- keep enough counters to prove each stage helped

Needed metrics:

- max transport lateness in microseconds
- max event queue depth
- queue overflow count
- late event count
- dropped event count
- max backend drain time
- max UI frame time

Exit condition:

- every later change can be judged against the same realtime contract

## Stage 1 - Turn The Existing Scheduler Into A Producer

Current situation:

- the engine already schedules future note events in tick space
- but those events are still dispatched by `processUntil(...)` when the loop arrives

Recommended change:

- keep `StepSequencerEngine` responsible for musical intent
- keep the sequencer event sink independent from the physical transport
- translate `SequencerEvent` work into realtime MIDI events with:
  - absolute deadline in microseconds
  - event type
  - channel
  - note
  - velocity
  - source track / engine id if useful for diagnostics

Why this is feasible:

- [NoteScheduler.hpp](../../open-control/note/src/oc/note/sequencer/NoteScheduler.hpp) already provides fixed-capacity event discipline
- [StepSequencerEngine.cpp](../../open-control/note/src/oc/note/sequencer/StepSequencerEngine.cpp) already schedules note on/off in advance

What must change:

- scheduler output becomes "enqueue event with deadline"
- direct transport emission moves out of the engine lane

## Stage 2 - Add A Realtime MIDI Event Queue

Recommended queue properties:

- fixed capacity
- no dynamic allocation
- deterministic push/pop cost
- explicit overflow policy

Recommended policy:

- small lateness: send and count
- large lateness: drop and count
- repeated lateness or queue pressure: resync and count

Important:

- never silently bunch stale note events after a UI stall

Why this matters:

- the current system can recover from stalls by catching up
- musically, "catch up by bursting" is often worse than "drop and recover deterministically"

## Stage 3 - Split Transport Backends Explicitly

### USB path

Goal:

- keep firmware-side timing deterministic
- bound and measure residual transport-layer uncertainty

Recommended path:

- queue events by absolute deadline
- drain frequently enough that USB buffering never causes internal backlog
- evaluate whether `send_now()` should be used:
  - always: lowest latency, potentially more USB overhead
  - selectively: flush after realtime clock or short bursts
  - never: simplest behavior, but more packet grouping delay

This should be a measured policy, not guessed.

### DIN / UART path

Goal:

- offer a stronger end-to-end realtime mode

Recommended path:

- add `TeensySerialMidi` in `hal-teensy`
- implement `IMidi` over a Teensy hardware serial port configured for MIDI
- allow composition with existing USB path only if semantics stay explicit

This is likely the cleanest path if "strictly musical realtime first" becomes a product requirement rather than only a firmware requirement.

## Stage 4 - Make Runtime State Consumption Coherent

Current risk:

- UI edits can arrive while playback is ongoing
- without explicit boundaries, the runtime can observe partially-updated intent

Recommended model:

- UI edits staging state
- runtime consumes committed snapshots
- commit happens at an explicit boundary:
  - immediate next tick for harmless parameters
  - next step for note/gate/nudge changes
  - next cycle or transport-safe boundary for structural edits

Why this matters:

- once we schedule events ahead of time, state coherence becomes more important, not less

## Stage 5 - Reduce UI Compute So UX Improves Too

This stage is not the guarantee mechanism, but it is still necessary.

Highest priority UI work based on current profiling:

1. Reduce first track-switch render cost in [SequencerView.cpp](../src/ui/view/SequencerView.cpp)
2. Reduce broad grid redraws in [StepGrid.cpp](../src/ui/sequencer/StepGrid.cpp)
3. Stop promoting runtime-only visual changes into full data redraws in [StepGridRenderLogic.cpp](../src/ui/sequencer/StepGridRenderLogic.cpp)
4. Defer or split expensive tint / header / strip updates when they are cosmetic only

The important rule is:

- UI work should be reduced because it improves UX and frees headroom
- but realtime correctness must not depend on UI staying cheap forever

## Stage 6 - Validation And Soak

Required stress scenarios:

- playback with 8 tracks
- repeated track switches during playback
- view selector open / close
- sequencer to macro switches
- overlay churn
- dense note patterns
- internal clock mode
- external clock mode
- USB backend
- DIN backend once implemented

Required validations:

- deadline miss counters
- queue occupancy and overflow
- note order
- note off pairing
- external capture when possible
- long soak runs, not only short manual checks

## What We Should Not Do

The following would be risky or short-lived:

- trying to solve the problem only with more UI micro-optimizations
- moving large parts of scheduling logic directly into ISR context
- allowing ISR code to call LVGL, event bus, allocation, or logging
- promising "perfect USB timing" without explicitly scoping it to device-side behavior
- mixing backend policy, scheduler policy, and UI policy into one service

## Recommended Order Of Work

This is the cleanest serious trajectory:

1. Freeze the contract and counters
2. Turn the existing engine scheduler into a producer of absolute-deadline MIDI events
3. Introduce a fixed-capacity output queue with explicit lateness policy
4. Separate USB policy from future DIN/UART policy
5. Add atomic runtime snapshot / commit boundaries
6. Reduce grid and track-switch UI cost so UX improves alongside correctness

## Stacked PR Execution Plan

This section turns the architecture recommendation into an execution plan that can actually be reviewed and merged incrementally.

The guiding rule is:

- each PR must strengthen the realtime boundary
- no PR may rely on "UI is probably cheap enough now" as its safety argument

### PR 1 - Freeze The Realtime Contract And Metrics

**Title**

- `core: freeze realtime MIDI contract and counters`

**Primary outcome**

- make the realtime contract visible in code and counters
- ensure the note send path is free of direct UI side effects

**Repos / ownership**

- `midi-studio/core`

**Likely files**

- [SequencerRuntimeService.cpp](../src/sequencer/SequencerRuntimeService.cpp)
- [SequencerPlaybackService.hpp](../src/sequencer/SequencerPlaybackService.hpp)
- [SequencerPlaybackService.cpp](../src/sequencer/SequencerPlaybackService.cpp)
- [REALTIME_MIDI_ISOLATION_PLAN.md](REALTIME_MIDI_ISOLATION_PLAN.md)
- [REALTIME_MIDI_FEASIBILITY_REPORT.md](REALTIME_MIDI_FEASIBILITY_REPORT.md)

**Deliverables**

- stable counters for:
  - max playback update cost
  - max runtime update cost
  - late note count
  - max tick jump
  - backend send max time
  - UI frame cost correlation
- note activity remains drained asynchronously after playback update, never inline with note emission

**Out of scope**

- queue redesign
- backend redesign
- state snapshots

**Main risks**

- adding measurement that itself perturbs the hot path
- mixing observability with policy

**Review checklist**

- no new allocations on the hot path
- no new LVGL mutation in note emission
- counters are bounded and cheap
- logs are periodic rather than per-event

**Exit criteria**

- profiling remains good enough to isolate scheduler, backend, and UI causes separately

### PR 2 - Convert The Engine Into A Producer Of Timestamped MIDI Work

**Title**

- `core: convert sequencer engine output to timestamped MIDI events`

**Primary outcome**

- keep the sequencer engine output transport-independent
- make the sink translate future MIDI work into absolute deadlines

**Repos / ownership**

- `open-control/note`
- `midi-studio/core`

**Likely files**

- [NoteScheduler.hpp](../../open-control/note/src/oc/note/sequencer/NoteScheduler.hpp)
- [StepSequencerEngine.hpp](../../open-control/note/src/oc/note/sequencer/StepSequencerEngine.hpp)
- [StepSequencerEngine.cpp](../../open-control/note/src/oc/note/sequencer/StepSequencerEngine.cpp)
- new `RealtimeMidiEvent` type in `core/src/sequencer/`
- [SequencerPlaybackService.hpp](../src/sequencer/SequencerPlaybackService.hpp)
- [SequencerPlaybackService.cpp](../src/sequencer/SequencerPlaybackService.cpp)

**Deliverables**

- a transport-independent event representation with at least:
  - deadline in absolute microseconds
  - type
  - channel
  - note
  - velocity
  - source track / engine id for diagnostics
- the engine schedules future work against a clock-derived deadline, not only "dispatch when loop reaches tick"

**Design note**

- keep the engine in tick space for musical correctness
- convert tick to absolute time at the scheduler/output boundary

**Out of scope**

- USB flush policy
- DIN backend
- UI optimization

**Main risks**

- breaking tick-to-time conversion during tempo changes
- introducing subtle phase errors on start / stop / continue

**Review checklist**

- no direct transport calls from the engine lane
- deadline math is monotonic and phase-preserving
- start/stop/resync behavior is explicitly tested

**Exit criteria**

- the engine can hand off future MIDI events without requiring immediate backend dispatch

### PR 3 - Introduce A Fixed-Capacity Realtime MIDI Queue

**Title**

- `core: add fixed-capacity realtime MIDI output queue`

**Primary outcome**

- make scheduler progress independent from backend send timing

**Repos / ownership**

- `midi-studio/core`

**Likely files**

- new `RealtimeMidiQueue.hpp/.cpp` in `src/sequencer/`
- [SequencerPlaybackService.hpp](../src/sequencer/SequencerPlaybackService.hpp)
- [SequencerPlaybackService.cpp](../src/sequencer/SequencerPlaybackService.cpp)
- [SequencerRuntimeService.cpp](../src/sequencer/SequencerRuntimeService.cpp)

**Deliverables**

- fixed-capacity queue
- constant-time or tightly bounded push/pop
- explicit counters:
  - queue high-water mark
  - overflow count
  - dropped event count
  - late send count
- explicit late-event policy

**Recommended late policy**

- slightly late: send immediately and count
- too late: drop and count
- repeated lateness: trigger resync strategy rather than bunching stale notes

**Out of scope**

- backend-specific flush semantics
- DIN backend

**Main risks**

- choosing a queue capacity that is too small
- hiding lateness behind implicit catch-up behavior

**Review checklist**

- no dynamic allocation
- overflow is observable and deterministic
- queue policy is documented in code comments and docs

**Exit criteria**

- scheduler never blocks on backend send
- late behavior is explicit and measurable

### PR 4 - Split USB Transport Policy From Realtime Scheduling

**Title**

- `open-control: separate USB MIDI drain policy from scheduler timing`

**Primary outcome**

- make USB transport behavior explicit instead of accidental

**Repos / ownership**

- `open-control/framework`
- `open-control/hal-teensy`
- `midi-studio/core`

**Likely files**

- [IMidi.hpp](../../open-control/framework/src/oc/interface/IMidi.hpp)
- [MidiAPI.cpp](../../open-control/framework/src/oc/api/MidiAPI.cpp)
- [UsbMidi.hpp](../../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.hpp)
- [UsbMidi.cpp](../../open-control/hal-teensy/src/oc/hal/teensy/UsbMidi.cpp)
- queue drain service in `core/src/sequencer/`

**Deliverables**

- a minimal transport-level flush or drain hook if needed
- documented USB policy:
  - never flush
  - always flush
  - selective flush
- instrumentation to compare those policies under stress

**Design note**

- do not add a broad abstraction unless the transport really needs it
- prefer a tiny optional capability over a hierarchy explosion

**Out of scope**

- DIN backend
- state snapshot system

**Main risks**

- overengineering the transport abstraction
- using a USB flush strategy that improves latency but hurts throughput or stability

**Review checklist**

- scheduler semantics do not change when transport policy changes
- transport policy is configurable or at least easy to benchmark
- USB-specific behavior does not leak back into engine code

**Exit criteria**

- USB timing behavior is measured and intentional, not incidental

### PR 5 - Add Atomic Runtime Snapshots For Playback-Critical State

**Title**

- `core: add playback snapshots and atomic sequencer commit boundaries`

**Primary outcome**

- prevent the runtime from observing mixed UI state during scheduling decisions

**Repos / ownership**

- `midi-studio/core`
- `open-control/note` only if engine interfaces need slight adaptation

**Likely files**

- `src/state/sequencer/`
- `src/sequencer/`
- [StepSequencerEngine.hpp](../../open-control/note/src/oc/note/sequencer/StepSequencerEngine.hpp)
- [StepSequencerEngine.cpp](../../open-control/note/src/oc/note/sequencer/StepSequencerEngine.cpp)

**Suggested first snapshot scope**

- enabled mask
- note
- velocity
- probability
- gate
- nudge
- midi channel
- division / length data required for scheduling

**Commit boundaries**

- next tick for harmless projection fields
- next step for step content
- next cycle or safe transport boundary for structural edits

**Out of scope**

- UI refactors
- transport backend additions

**Main risks**

- too-large snapshots copied too often
- undefined commit semantics between editing features

**Review checklist**

- runtime never reads half-old half-new scheduling state
- snapshot boundaries are explicit per edit family
- copy cost is measured

**Exit criteria**

- edits under playback no longer create transient malformed scheduling states

### PR 6 - Add A Teensy DIN / UART MIDI Backend

**Title**

- `hal-teensy: add hardware serial MIDI transport`

**Primary outcome**

- provide a transport with a stronger end-to-end realtime story than USB

**Repos / ownership**

- `open-control/hal-teensy`
- possibly small plumbing in `open-control/framework`

**Likely files**

- new `TeensySerialMidi.hpp/.cpp` in `open-control/hal-teensy/src/oc/hal/teensy/`
- [AppBuilder.hpp](../../open-control/hal-teensy/src/oc/hal/teensy/AppBuilder.hpp)
- optional config wiring in app bootstrap

**Deliverables**

- `IMidi` implementation over Teensy hardware serial MIDI
- selectable serial port
- active note tracking parity with USB backend
- input clock timestamp capture as close to ingress as possible

**Out of scope**

- multimode transport aggregation unless clearly needed

**Main risks**

- pin/config complexity
- support burden if too many serial configurations are exposed at once

**Review checklist**

- UART backend semantics match USB backend where they should
- clock input timestamping remains explicit
- all-notes-off behavior is equivalent

**Exit criteria**

- the firmware can run in a strong realtime DIN mode with the same engine contract

### PR 7 - Reduce Sequencer UI Compute Without Re-Coupling Realtime

**Title**

- `core: reduce sequencer render churn and broad grid invalidation`

**Primary outcome**

- improve UX and free CPU/GPU bandwidth without making correctness depend on UI thrift

**Repos / ownership**

- `midi-studio/core`

**Likely files**

- [SequencerView.cpp](../src/ui/view/SequencerView.cpp)
- [SequencerView.hpp](../src/ui/view/SequencerView.hpp)
- [StepGrid.cpp](../src/ui/sequencer/StepGrid.cpp)
- [StepGridRenderLogic.cpp](../src/ui/sequencer/StepGridRenderLogic.cpp)
- [StepGridFrameLogic.cpp](../src/ui/sequencer/StepGridFrameLogic.cpp)
- possibly [MacroView.cpp](../src/ui/view/MacroView.cpp) for symmetry

**First high-value cuts**

- stop treating `probabilityCycleActiveChanged` as a full data redraw when a lighter visual update is sufficient
- reduce first track-switch full render scope
- split cosmetic rerenders across frames where safe
- keep root tint/header/strip work incremental

**Out of scope**

- changing the realtime contract

**Main risks**

- visual regressions
- overcomplicating diff logic

**Review checklist**

- fewer dirty tiles during playback
- lower first track-switch render cost
- no new coupling from UI back into scheduling logic

**Exit criteria**

- the UI is materially lighter, but realtime correctness would still hold even if future UI work regresses

## Merge Strategy

Recommended merge strategy:

1. PR 1 is the baseline and can merge first
2. PR 2 and PR 3 should be stacked directly
3. PR 4 depends on PR 3
4. PR 5 should merge before heavy editing features are expanded further
5. PR 6 is optional for USB-only releases but strongly recommended for a stricter realtime product mode
6. PR 7 can run in parallel once the realtime boundary is no longer shared

## Minimal Success Path

If the team wants the smallest path that still delivers the central guarantee for the current USB product, the minimum serious path is:

1. PR 1
2. PR 2
3. PR 3
4. PR 4
5. PR 5

This gives:

- device-side authoritative time
- scheduler/backend decoupling
- explicit late-event policy
- a clearer USB timing story
- coherent runtime state under edits

PR 7 then improves UX.

PR 6 strengthens transport guarantees further.

## Final Recommendation

The project should proceed with this exact framing:

- **Primary mission**: make engine timing and outgoing MIDI immune to UI overload at the firmware architecture level
- **Secondary mission**: reduce UI compute so the interface remains pleasant and does not waste budget
- **Transport truth**:
  - USB can be made firmware-correct and well-bounded
  - DIN/UART is the best path for the strongest end-to-end musical guarantee

This refactor is worth doing because:

- the current hotspots are now well identified
- the codebase already contains the right primitives to evolve toward the target architecture
- the remaining work is more about choosing clean separation boundaries than inventing a new engine from scratch
