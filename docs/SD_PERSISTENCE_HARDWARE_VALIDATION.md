# SD Persistence Hardware Validation

Branch target: `feature/sd-persistence-data-manager`

This checklist is intended for Teensy + SD manual validation after the
persistence/data-manager feature set.

## Preconditions

- Firmware flashed from current branch.
- SD card inserted and writable.
- Boot reaches normal UI (no assertion in serial monitor).

## Quick Smoke

1. Power cycle twice.
2. Confirm boot succeeds both times.
3. Confirm no `Signal: MaxSubscribers exceeded` assertion appears.

Expected: stable boot, app responsive, no fatal assertion.

## Data Manager Entry + Navigation

1. Go to Macro root view.
2. Hold `NAV` for ~2s.
3. Confirm Data Manager overlay opens.
4. Rotate `NAV` to move row focus.
5. Rotate `OPT` to edit selected row value.
6. Press `LEFT_TOP` to close.

Expected: open/close works; row focus and value editing respond correctly.

## Macro Library

1. Set `Target=MACRO`, `Action=SAVE`, `Slot=N`, execute.
2. Change some macro values and at least one macro config.
3. Set `Action=LOAD`, same slot, execute.
4. Verify values/config are restored.
5. Set `Action=ERASE`, same slot, execute.
6. Try `LOAD` again.

Expected: save/load roundtrip works; erased slot no longer restores saved state.

## Sequencer Pattern Library

1. Build a recognizable pattern (length/division/channel + a few enabled steps).
2. `Target=PATTERN`, `Action=SAVE`, `Slot=N`, execute.
3. Modify pattern significantly.
4. `Action=LOAD`, same slot, execute while transport stopped.

Expected: immediate replace when stopped.

## Sequencer Pattern Load While Playing

1. Start transport.
2. Trigger pattern load from Data Manager.
3. Watch one step boundary.

Expected: pattern does not jump mid-step; apply occurs on next step.

## Sequencer Set: Replace/Merge Prompt

1. Set `Target=SET`, `Action=LOAD`, `Slot=N`, execute.
2. Confirm mode selector appears with `REPLACE` and `MERGE`.
3. Confirm default selection is `REPLACE`.
4. Confirming selection should execute load (selector must not reopen in a loop).

Expected: one selector interaction executes load directly.

## Sequencer Set Replace

1. Save a set slot from a known pattern.
2. Create a very different live pattern.
3. Load saved set with `REPLACE`.

Expected: full set pattern replacement behavior.

## Sequencer Set Merge

1. Save a set slot with only specific enabled steps.
2. Build a live pattern with different enabled steps and different timing/channel.
3. Load saved set with `MERGE`.

Expected:
- incoming enabled steps overwrite those step payloads,
- existing enabled steps not present in incoming remain,
- current timing/channel remain unchanged.

## Sequencer Set Load While Playing

1. Start transport.
2. Load set in `REPLACE`, then in `MERGE`.

Expected: both are quantized to next-step apply while playing.

## Persistence Recovery Sanity

1. Save at least one macro slot, one pattern slot, one set slot.
2. Power cycle.
3. Verify workspace auto-restore and explicit slot loads still work.

Expected: persisted data remains usable after reboot.

## Optional Stress Loop

Repeat 20 times:

1. Edit data.
2. Save slot.
3. Load slot.
4. Switch views.

Expected: no crash, no lock-up, no obvious data corruption.
