# Sprint 4: Persistence Compatibility And Failure Semantics

Updated: 2026-04-29

Purpose: make storage format compatibility and failure behavior reviewable
before any persistence migration or SD-card recovery work.

Current status: Sprint 4 software tranche complete for compatibility, failure
semantics, and Teensy main-loop recovery wiring. It documents the active storage
domains, hardens unavailable-storage status reporting, keeps transient
unavailable writes pending, adds the pure recovery state machine, wires it in
`main.cpp`, and exposes the RAM-authoritative `CoreState` recovery operation. No
binary payload migration was introduced.

## Scope

Included:

- Inventory active storage domains, file names, versions, slot counts, payload
  sizes, and tests.
- Clarify current compatibility policy.
- Clarify current SD failure and hot-swap behavior.
- Add native tests for current compatibility/failure semantics where they are
  already expressible without hardware.

Excluded:

- Adding a new payload version or migration format.
- Generating released binary fixture files without an actual released-format
  baseline.
- Hardware validation of media removal, SDIO timing, or flush latency.

## Current Source Checks

Run from `midi-studio/core` or anywhere in the workspace:

```powershell
ms test core
rg -n "WORKSPACE_DATA_VERSION|LIBRARY_DATA_VERSION|FILE_FORMAT_VERSION|VERSION|MAGIC|SLOT_COUNT|PAYLOAD_SIZE" src/persistence src/state -g "*.hpp" -g "*.cpp"
rg -n "STORAGE_UNAVAILABLE|COMMIT_FAILED|CRC_MISMATCH|HEADER_MISMATCH|reopen\\(" src test main.cpp
```

Current verification:

- `ms test core` passes `45/45`.
- `ms build core --target teensy` passes.
- `test_CoreSettings` covers current v2 roundtrip, invalid-version reset, and
  v1 settings compatibility.
- `test_CoreSettingsFailures` covers short write/read, commit failure, erase
  failure, and unavailable-storage statuses.
- `test_PersistenceSlotFileStore` covers empty formatting, roundtrip, CRC
  mismatch, latest-slot fallback, and unavailable-storage statuses.
- `test_CoreStatePersistence` covers settings/macro/shared-track pending-save
  retention across transient unavailable storage and RAM-authoritative recovery
  after storage reopen.
- `test_StorageRecoveryMachine` covers removal debounce, insertion debounce,
  play-safe recovery deferral, reopen failure backoff, and removed-during-pending
  recovery.
- `test_MacroPersistence` and `test_SequencerPersistence` cover explicit
  library roundtrips, library bounds, erase, and sequencer mask sanitization.
- Current project/session state is stored through project `.mspj` snapshots.
  Macro and sequencer domain persistence is reserved for explicit reusable
  libraries, not automatic session recovery.

## Storage Domain Matrix

| Domain | Runtime file | Owner | Format identity | Slots | Payload | Native coverage |
|---|---|---|---|---:|---:|---|
| Core settings | `/macros.bin` | `CoreSettingsLayout` / `CoreSettingsCodec` | magic `MCST`, version `2` | compact byte layout | `STORAGE_END = 17` bytes | `test_CoreSettings`, `test_CoreSettingsFailures` |
| Macro library | `/macro-library.bin` | `MacroPersistence` | magic `MLIB`, domain version `1` | 16 direct slots | 14404 bytes | `test_MacroPersistence` |
| Sequencer pattern library | `/sequencer-pattern-library.bin` | `SequencerPersistence` / codec payloads | magic `SPLB`, domain version `3` | 32 direct slots | envelope up to 65520 bytes | `test_SequencerPersistence` |
| Sequencer set library | `/sequencer-set-library.bin` | `SequencerPersistence` / codec payloads | magic `SSET`, domain version `3` | 16 direct slots | envelope up to 65520 bytes | `test_SequencerPersistence` |

Notes:

- `PersistenceSlotFileStore::FILE_FORMAT_VERSION` is `1` for all slot-file
  domains. Domain versions are separate and are owned by each codec/domain.
- The `/macros.bin` settings file name is current firmware behavior. Rename it
  only as an explicit migration.
- Slot-file payloads are byte-copied. Field order, packing, size, and version
  must move together.

## Compatibility Policy

Current guaranteed compatibility:

- Core settings version `1` can still load MIDI sync settings, legacy shortcut
  offsets `0x000A..0x000D`, and default shared-track state.
- Core settings version `2` owns the current shared-track and shortcut layout.
- Unknown future settings versions reset to defaults and persist the current
  version.
- Slot-file domains reject magic/version/layout mismatches instead of attempting
  implicit migration.

Before any future payload change:

- Add or capture binary fixtures for the released old format.
- Bump the domain data version when byte layout changes.
- Keep migration code narrow and delete it only after the product explicitly
  drops that compatibility promise.
- Update the matrix above and the relevant native tests in the same patch.

## Failure Semantics

Current behavior:

- Boot-time SD initialization is fail-fast in `main.cpp`: every storage backend
  must initialize or firmware halts during startup.
- `PersistenceSlotFileStore` returns `STORAGE_UNAVAILABLE` when its backend is
  unavailable before format/save/load/latest/erase operations.
- `CoreSettingsCodec` now checks `backend.available()` before exact reads and
  writes. Settings save/commit/factory-reset paths report
  `STORAGE_UNAVAILABLE` instead of collapsing unavailable storage into generic
  IO/commit errors.
- Workspace journals can fall back from the newest corrupted slot to an older
  valid slot.
- CRC mismatch, header mismatch, out-of-range slots, short writes, commit
  failures, and erase failures are represented as explicit statuses.

Current SD hot-swap policy:

- Runtime hot-swap recovery is wired in `main.cpp` through a main-loop
  `StorageRecoveryRuntimeManager`.
- The manager samples the six SD backends at a slow cadence, defers recovery
  while transport is playing, reopens every backend together, and then calls
  `CoreState::recoverPersistenceFromRamAfterStorageReopen()`.
- `core::persistence::StorageRecoveryMachine` owns the pure media state
  transitions; `main.cpp` owns concrete backend presence/reopen calls.
- Therefore the supported behavior is: storage must be present at boot; runtime
  failures surface through unavailable/IO/commit statuses where the storage API
  can observe them; runtime reinsert attempts RAM-authoritative recovery.
  Physical removal/reinsert behavior still requires Teensy hardware validation.

## Clean Recovery Strategy

The clean strategy is media-level recovery, not per-file opportunistic recovery.
All six firmware storage files live on the same SD card, so the recovery owner
should treat the card as one medium and reopen every backend together.

Core decision:

- At runtime, RAM is authoritative.
- After media removal/reinsert, do not load SD data into the live session.
- Recovery should revalidate/reopen storage and then preserve the current RAM
  session as authoritative while restoring settings/library write readiness.
- Library slots on the recovered card are left as card-owned content unless the
  user explicitly saves/erases/loads them through Data Manager.

Why:

- Loading on hot-swap can silently replace the active performance/session with
  stale data from another card.
- Treating RAM as authoritative preserves the musical state the user was editing
  while storage was absent.
- Library slots are user-managed assets; automatic recovery should not bulk
  overwrite them.

Recommended owner:

- A Teensy-only `StorageRecoveryManager` near `main.cpp`, because it owns
  `SDCardBackend::available()` / `reopen()` and the storage backend list.
- `CoreState` should expose a narrow persistence-recovery operation, not direct
  backend access. The operation should reinitialize explicit domain stores and
  persist live settings without loading retired domain stores from storage.

Suggested state machine:

| State | Meaning | Exit condition |
|---|---|---|
| `READY` | All storage backends are available. Normal persistence is enabled. | Any backend reports unavailable for the debounce window. |
| `MISSING_DEBOUNCE` | A missing-media sample was observed. | Media returns before debounce, or debounce expires. |
| `OFFLINE` | SD is absent/unusable. Persistence writes are suspended or retained for retry. | Media is present for N consecutive samples. |
| `RECOVERY_PENDING` | Media looks present again, but recovery is not safe yet. | Playback is stopped or the app reaches the chosen recovery-safe point. |
| `REOPENING` | Close/reopen all file handles. | All `reopen()` calls succeed or one fails. |
| `REVALIDATING` | Re-init persistence domains and save RAM-authoritative settings. | All required init/save operations succeed or one fails. |
| `READY_RECOVERED` | Storage is back online. Normal persistence resumes. | Fold back into `READY` after logging/UI feedback. |
| `DEGRADED` | Recovery failed while media is present. | Backoff expires and recovery is retried, or media is removed again. |

Operational rules:

- Sample media at a slow cadence, for example 500-1000 ms, not every app tick.
- Debounce removal and insertion; SD mechanical/media signals can bounce.
- Do not call `reopen()` from realtime playback code or a timer lane.
- Prefer deferring recovery while transport is playing. Runtime playback should
  continue from RAM if storage disappears.
- When storage is offline, persistence failures must not erase dirty intent.
  Pending project/settings saves should remain pending or be requeued when the
  status is `STORAGE_UNAVAILABLE`.
- On recovery success, save current settings/shared-track state and reinitialize
  explicit library stores. Do not auto-save every library slot.
- On recovery failure, stay degraded and retry with backoff; avoid log spam.

Implementation sequence:

1. Complete: add a pure native-tested recovery state machine.
   Inputs: `mediaPresent`, `playing`, `nowMs`, and operation results.
   Outputs: `none`, `markOffline`, `attemptReopen`, `attemptRevalidate`,
   `markRecovered`, `markDegraded`.

2. Complete: make pending writes recovery-safe.
   `STORAGE_UNAVAILABLE` should keep or restore pending persistence flags, so a
   transient SD loss does not discard unsaved RAM changes.

3. Complete: add a narrow CoreState recovery API.
   `recoverPersistenceFromRamAfterStorageReopen()` re-inits settings and
   macro/sequencer library stores, then saves current settings without loading
   retired domain stores from storage.

4. Complete: wire a Teensy-only recovery manager in `main.cpp`.
   It should own the backend list, sample cadence, reopen attempts, and logging.

5. Partial: add feedback.
   Current implementation logs `SD missing`, `SD recovered`, and recovery
   failure/degraded events. A dedicated visible UI warning can be added later
   without overloading page names.

6. Validate on hardware.
   Test boot without SD, remove while idle, remove while playing, insert same
   card, insert different card, failed reopen, and write after recovery.

Non-goals for the first implementation:

- No automatic library slot merge.
- No automatic reload from SD after runtime recovery.
- No recovery attempt inside interrupt/timer playback paths.
- No unbounded retry loop.

## Completed Sprint 4 Changes

- Added unavailable-storage tests for `PersistenceSlotFileStore`.
- Added unavailable-storage tests for `CoreSettings`.
- Added CoreSettings v1 compatibility regression for legacy shortcut offsets
  and default shared-track state.
- Updated settings read/write/commit/factory-reset paths to check
  `backend.available()` before reporting status.
- Added `StorageRecoveryMachine` with native coverage for debounce, safe-point
  deferral, failed reopen backoff, and removed-during-pending transitions.
- Updated project/session and shared-track pending saves so
  `STORAGE_UNAVAILABLE` preserves dirty intent instead of clearing it.
- Added `CoreState::recoverPersistenceFromRamAfterStorageReopen()` and native
  coverage proving recovered storage is revalidated from RAM authority, not
  reloaded from a stale card snapshot.
- Wired `main.cpp` to sample SD availability, reopen all storage backends, defer
  recovery while playing, and revalidate from live RAM.

## Exit Criteria

This Sprint 4 tranche is complete when:

- The storage domain matrix is documented.
- Current compatibility promises are explicit.
- Unavailable-storage behavior has native coverage.
- Existing CoreSettings v1 compatibility has native coverage.
- `ms test core` passes.

Current result: complete for this software tranche, with `ms test core` passing
`45/45` and `ms build core --target teensy` passing.

Remaining future Sprint 4 work requires hardware or release-process input:

- Capture real released binary fixtures before any format migration.
- Add dedicated user-visible status feedback for missing, recovered, and
  degraded SD states if product UX requires it.
- Validate removal/reinsert behavior on Teensy hardware.
