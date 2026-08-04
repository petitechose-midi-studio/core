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
