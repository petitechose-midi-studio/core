# SD Persistence Implementation Tracker

> Owner: OpenCode agent
> Branch: `feature/sd-persistence-data-manager`
> Last update: 2026-02-20

## Objective

Implement SD persistence with deterministic runtime behavior and testable incremental delivery:

- Auto-restore workspace on boot
- Explicit save/load/erase libraries for macro bank, sequencer pattern, sequencer set
- Safe playback-time apply (`next step`) for runtime consistency
- Corruption-tolerant storage with clear fallbacks

## Delivery Plan (living)

| Iteration | Scope | Status | Notes |
|---|---|---|---|
| 0 | Branch + tracker + test strategy baseline | DONE | Branch created, tracker initialized |
| 1 | Versioned binary storage foundation (format + CRC + slots) | DONE | Implemented in `src/persistence/PersistenceSlotFileStore.hpp` with tests |
| 2 | Macro domain: workspace + library slots + integration in `CoreState` | DONE | Workspace auto-save/load + library APIs wired in state |
| 3 | Sequencer domain: workspace + pattern/set libraries | DONE | Workspace + pattern/set slots integrated and tested |
| 4 | Playback-safe apply queue (`next step`) | DONE | Pattern/Set loads are staged and applied on next playhead step while playing |
| 5 | Data Manager UI flow (`NAV` long press) + Replace/Merge prompt | DONE | Added Data Manager overlay + Set load mode selector (default `Replace`) |
| 6 | Migration/fallback hardening + regression tests + perf validation | TODO | Record final deviations |

## Test Strategy

Each iteration must add/adjust tests before finalizing code changes.

| Iteration | Test target | Command | Status |
|---|---|---|---|
| 1 | Persistence format/CRC/slot behavior | `g++ -std=c++17 test/test_PersistenceSlotFileStore/test_main.cpp -I src -I ../../open-control/framework/src -o .cache/test_PersistenceSlotFileStore.exe && .cache/test_PersistenceSlotFileStore.exe` | PASS |
| 1 | Existing CoreSettings regression | `g++ -std=c++17 test/test_CoreSettings/test_main.cpp ../../open-control/framework/src/oc/state/NotificationQueue.cpp -I src -I ../../open-control/framework/src -o .cache/test_CoreSettings.exe && .cache/test_CoreSettings.exe` | PASS |
| 2 | Macro persistence workspace/library behavior | `g++ -std=c++17 test/test_MacroPersistence/test_main.cpp -I src -I ../../open-control/framework/src -o .cache/test_MacroPersistence.exe && .cache/test_MacroPersistence.exe` | PASS |
| 2 | CoreState integration for macro persistence | `g++ -std=c++17 test/test_CoreStatePersistence/test_main.cpp ../../open-control/framework/src/oc/state/NotificationQueue.cpp ../../open-control/framework/src/oc/time/Time.cpp -I src -I ../../open-control/framework/src -I ../../open-control/note/src -o .cache/test_CoreStatePersistence.exe && .cache/test_CoreStatePersistence.exe` | PASS |
| 2 | Firmware compile check after multi-backend migration | `pio run -e dev` | PASS |
| 3 | Sequencer persistence workspace/pattern/set behavior | `g++ -std=c++17 test/test_SequencerPersistence/test_main.cpp ../../open-control/framework/src/oc/state/NotificationQueue.cpp -I src -I ../../open-control/framework/src -I ../../open-control/note/src -o .cache/test_SequencerPersistence.exe && .cache/test_SequencerPersistence.exe` | PASS |
| 3 | CoreState integration for sequencer persistence | `g++ -std=c++17 test/test_CoreStatePersistence/test_main.cpp ../../open-control/framework/src/oc/state/NotificationQueue.cpp ../../open-control/framework/src/oc/time/Time.cpp -I src -I ../../open-control/framework/src -I ../../open-control/note/src -o .cache/test_CoreStatePersistence.exe && .cache/test_CoreStatePersistence.exe` | PASS |
| 3 | Firmware compile check after sequencer integration | `pio run -e dev` | PASS |
| 4 | Quantized next-step apply during playback | `g++ -std=c++17 test/test_CoreStatePersistence/test_main.cpp ../../open-control/framework/src/oc/state/NotificationQueue.cpp ../../open-control/framework/src/oc/time/Time.cpp -I src -I ../../open-control/framework/src -I ../../open-control/note/src -o .cache/test_CoreStatePersistence.exe && .cache/test_CoreStatePersistence.exe` | PASS |
| 4 | Firmware compile check after quantized apply | `pio run -e dev` | PASS |
| 5 | Data Manager + set merge semantics regression | `g++ -std=c++17 test/test_CoreStatePersistence/test_main.cpp ../../open-control/framework/src/oc/state/NotificationQueue.cpp ../../open-control/framework/src/oc/time/Time.cpp -I src -I ../../open-control/framework/src -I ../../open-control/note/src -o .cache/test_CoreStatePersistence.exe && .cache/test_CoreStatePersistence.exe` | PASS |
| 5 | Firmware compile check after Data Manager integration | `pio run -e dev` | PASS |

Notes:

- We keep tests executable on host (fast loop) for each checkpoint.
- We keep SD/runtime integration verification for later embedded validation pass.

## Decisions Log

| Date | Decision | Rationale |
|---|---|---|
| 2026-02-20 | Data Manager entry is `NAV` long press (Macro + Sequencer views) | Avoid collision with existing overlay bindings |
| 2026-02-20 | Load Set shows `Replace/Merge`, default highlight = `Replace` | Safer explicit behavior and clear user intent |
| 2026-02-20 | Sequencer set snapshots are autonomous (no slot reference indirection) | Deterministic portability and simpler restore semantics |
| 2026-02-20 | Runtime apply while playing is quantized to next step | Prevent temporal glitches and transport discontinuities |
| 2026-02-20 | Metadata uses counters/relative time, not wall-clock date | No reliable RTC source in current architecture |
| 2026-02-20 | New persistence low-level format = file header + fixed slots + per-slot CRC32 | Allows deterministic validation and slot-level corruption isolation |
| 2026-02-20 | Slot writes use two-phase state transition (`WRITING` then `VALID`) | Reduces false-valid reads on torn/incomplete writes |
| 2026-02-20 | `loadLatest` falls back to previous valid slot if newest payload CRC fails | Improves resilience after partial corruption |
| 2026-02-20 | Macro workspace persistence uses dual-slot journal (2 slots) | Keeps last valid snapshot available if latest write is corrupted |
| 2026-02-20 | Macro library capacity starts at 16 slots | Practical initial UX target with bounded storage footprint |
| 2026-02-20 | Macro persistence is integrated in `CoreState` and updated on value/page/config writes | Keeps runtime behavior compatible while enabling new save/load APIs |
| 2026-02-20 | Macro persistence moved from slice-based layout to dedicated storage backends/files | Aligns with target domain isolation and removes temporary address partitioning |
| 2026-02-20 | Runtime macro edits no longer mirror writes into `CoreSettings` on normal path | Reduces duplicate SD writes while keeping migration source readable |
| 2026-02-20 | Sequencer persistence uses dedicated workspace/pattern/set backends and `CoreState` autosave watchers | Keeps domains isolated and captures sequencer edits with debounce |
| 2026-02-20 | Sequencer pattern library capacity starts at 32 slots and set library at 16 slots | Practical initial footprint while keeping UI/library scalable |
| 2026-02-20 | Sequencer set payload v1 is autonomous but currently mono-track (`trackCount=1`) | Preserves autonomous contract now, leaves multi-track extension to a future payload version |
| 2026-02-20 | Sequencer Pattern/Set loads while playing are queued and applied when playhead advances | Avoids mid-step mutations and transport discontinuities |
| 2026-02-20 | Queued sequencer load applies immediately when transport is stopped | Keeps user intent deterministic outside playback |
| 2026-02-20 | Data Manager opens via `NAV` long press from Macro/Sequencer root scopes | Keeps persistence ops discoverable without colliding with short-press navigation |
| 2026-02-20 | Data Manager Set load uses explicit selector prompt (`REPLACE`/`MERGE`) with default `REPLACE` | Preserves safe default while allowing additive workflow |
| 2026-02-20 | Sequencer set `MERGE` overlays incoming enabled steps onto current pattern and preserves current timing/channel | Supports additive set recall without destructive transport/config replacement |

## Deviations / Gaps Log

- Host regression command for `test_CoreSettings` requires linking `NotificationQueue.cpp` explicitly in this repo setup.
- `CoreSettings` still contains legacy macro fields in its schema; they are currently migration/fallback data rather than primary runtime persistence.

## Progress Journal

- 2026-02-20 / Checkpoint 1: branch created (`feature/sd-persistence-data-manager`), tracker initialized.
- 2026-02-20 / Checkpoint 2: added `PersistenceSlotFileStore` low-level storage primitive with format checks and CRC.
- 2026-02-20 / Checkpoint 3: added `test/test_PersistenceSlotFileStore/test_main.cpp` and validated corruption fallback behavior.
- 2026-02-20 / Checkpoint 4: re-ran legacy `CoreSettings` tests to verify no immediate regression.
- 2026-02-20 / Checkpoint 5: added `MacroPersistence` service (workspace + library) on top of slot store.
- 2026-02-20 / Checkpoint 6: added `test/test_MacroPersistence/test_main.cpp` and validated workspace/library flows.
- 2026-02-20 / Checkpoint 7: added `StorageSlice` adapter and test coverage for bounded offset mapping.
- 2026-02-20 / Checkpoint 8: integrated macro persistence service into `CoreState` (workspace auto-save/load + library slot APIs).
- 2026-02-20 / Checkpoint 9: added `test/test_CoreStatePersistence/test_main.cpp` to lock integration behavior.
- 2026-02-20 / Checkpoint 10: migrated to dedicated storages (`settings`, `macro-workspace`, `macro-library`) and removed `StorageSlice` temporary layer.
- 2026-02-20 / Checkpoint 11: firmware compile check passed (`pio run -e dev`) after constructor/storage wiring changes.
- 2026-02-20 / Checkpoint 12: added `SequencerPersistence` service (`workspace`, `pattern library`, `set library`) with slot-store backing.
- 2026-02-20 / Checkpoint 13: integrated sequencer persistence into `CoreState` with debounced autosave watchers and public slot APIs.
- 2026-02-20 / Checkpoint 14: added/extended tests (`test_SequencerPersistence`, `test_CoreStatePersistence`) and validated firmware build.
- 2026-02-20 / Checkpoint 15: implemented playback-safe queued apply for sequencer Pattern/Set loads (`next step` quantization).
- 2026-02-20 / Checkpoint 16: extended `test_CoreStatePersistence` with explicit quantized-apply coverage.
- 2026-02-20 / Checkpoint 17: firmware compile check passed (`pio run -e dev`) after quantized apply integration.
- 2026-02-20 / Checkpoint 18: added Data Manager overlay flow (open via `NAV` long press, Target/Action/Slot controls).
- 2026-02-20 / Checkpoint 19: added Set load mode selector prompt (`REPLACE`/`MERGE`, default `REPLACE`) and wired execution flow.
- 2026-02-20 / Checkpoint 20: implemented sequencer set merge mode in `CoreState` and added host regression coverage.
- 2026-02-20 / Checkpoint 21: firmware compile check passed (`pio run -e dev`) after Data Manager integration.

## Handover Notes

- This file is the source of truth for implementation state.
- Any scope deviation, behavior adjustment, or technical compromise must be logged here before commit.
- For every commit: update iteration status, tests status, and decision/deviation tables.
