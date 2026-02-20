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
| 3 | Sequencer domain: workspace + pattern/set libraries | IN_PROGRESS | Set snapshots autonomous |
| 4 | Playback-safe apply queue (`next step`) | TODO | No timeline jump behavior change |
| 5 | Data Manager UI flow (`NAV` long press) + Replace/Merge prompt | TODO | Replace default highlighted |
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

## Handover Notes

- This file is the source of truth for implementation state.
- Any scope deviation, behavior adjustment, or technical compromise must be logged here before commit.
- For every commit: update iteration status, tests status, and decision/deviation tables.
