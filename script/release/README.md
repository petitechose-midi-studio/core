# Teensy post-link release interface

`teensy_post_link_gate_v1.py` is the Core-owned, dependency-free boundary for
Teensy 4.1 product memory and allocated-ELF topology. A consumer supplies its
own versioned product profile and the textual outputs of `teensy_size`,
`arm-none-eabi-readelf -W -S` and `arm-none-eabi-nm --defined-only
--format=posix --radix=x`.

The module never imports product symbols or product thresholds. Core keeps its
symbol placement checks in the existing PlatformIO adapter; Bitwig adds its
consumer in `L-R11-02B`.

Example over captured metadata:

```text
python script/release/teensy_post_link_gate_v1.py \
  --profile product-profile.json \
  --teensy-size-output teensy-size.txt \
  --readelf-sections-output sections.txt \
  --nm-symbols-output symbols.txt
```

The old `script/dev/teensy_memory_budget.py` and
`script/dev/teensy_elf_topology.py` imports are exact forwarding façades under
`CL-ELF-GATE`. They expire at `L-R11-02R`; no new consumer may use them.

Core release builds select
`profiles/core-product-profile-v1.json` through `platformio.ini`. The real
PlatformIO post-link action applies the v1 gate and writes `teensy-size.txt`,
`sections.txt`, `symbols.txt` and `report.json` beside the release ELF under
`.pio/build/release/post-link`. It also requests `firmware.map` from the linker.
`validate_core_candidate_artifacts.py` then fails closed unless the promoted CI
artifact contains exactly the expected HEX, ELF, map, green report and matching
profile.

The active Flash ceilings are rounded operational budgets, not byte-exact
snapshots: 1180 KiB code, 296 KiB data and 10 KiB headers. Against the
`d040292` Drum-sequencer release image this retains roughly 51.5 KiB, 17.6 KiB
and 1.9 KiB of headroom respectively. Flash budget overruns are advisory: every
release reports and archives the exact drift without blocking publication.
Physical Flash capacity, RAM1, ITCM, RAM2, PSRAM and ELF placement remain strict
safety gates, including exactly nine ITCM banks. A threshold change requires an
explicit profile-version and baseline update.
