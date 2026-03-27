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

## Context Overlay Entry + Softkeys

1. Go to Macro root view, then to Sequencer root view.
2. In each view, hold `NAV` for ~2s.
3. Confirm the context overlay opens in both views (same trigger).
4. Confirm bottom softkey bar appears and transport bar is hidden while overlay is visible.
5. Confirm bindings in overlay context:
   - `BOTTOM_LEFT` runs left shortcut,
   - `BOTTOM_CENTER` opens full command list,
   - `BOTTOM_RIGHT` runs right shortcut,
   - `NAV` turn changes focused mapping row,
   - `NAV` release opens mapping selector,
   - `LEFT_TOP` closes dialog/overlay.

Expected: context overlay takes input authority; play/page buttons are temporarily overridden.

## Shortcut Mapping + Persistence

1. In Macro context overlay, map left/right shortcuts to non-default commands.
2. In Sequencer context overlay, map left/right shortcuts to non-default commands.
3. Close overlay and re-open: verify mappings persist in-session.
4. Power cycle device and re-open overlays.

Expected: macro/sequencer shortcut mappings are restored after reboot.

## Macro Context Commands

1. Run `Save Macro` (shortcut or command palette), choose slot, confirm if overwrite is prompted.
2. Change macro values/config.
3. Run `Load Macro` from same slot.
4. Run `Erase Macro` (must require confirmation).
5. Run `Load Macro` again from erased slot.

Expected: save/load roundtrip works, erase requires confirm, loading erased slot reports non-success.

## Sequencer Pattern Commands

1. Build a recognizable pattern (length/division/channel + enabled steps).
2. Run `Save Pattern`, choose slot (confirm overwrite when applicable).
3. Modify pattern significantly.
4. Run `Load Pattern` with transport stopped.

Expected: immediate replace when stopped.

## Sequencer Pattern Load While Playing

1. Start transport.
2. Trigger `Load Pattern` from context overlay.
3. Watch one step boundary.

Expected: pattern does not jump mid-step; apply occurs on next step.

## Sequencer Set Commands + Replace/Merge

1. Run `Load Set` from sequencer context.
2. Choose slot.
3. Confirm mode selector appears with `REPLACE` and `MERGE`.
4. Confirm default mode selection is `REPLACE`.

Expected: load mode selection appears after slot selection and executes once (no reopen loop).

## Sequencer Set Replace

1. Save a set slot from a known pattern.
2. Create a very different live pattern.
3. Load saved set with mode `REPLACE`.

Expected: full set pattern replacement behavior.

## Sequencer Set Merge

1. Save a set slot with only specific enabled steps.
2. Build a live pattern with different enabled steps and different timing/channel.
3. Load saved set with mode `MERGE`.

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
4. Verify shortcut mappings are still retained.

Expected: persisted data remains usable after reboot.

## Optional Stress Loop

Repeat 20 times:

1. Edit data.
2. Save slot.
3. Load slot.
4. Switch views.

Expected: no crash, no lock-up, no obvious data corruption.
