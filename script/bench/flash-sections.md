# ILI9341 Flash-section candidate

The three `*_flash_candidate` environments override only the ILI9341_T4
dependency, pinning `open-control/ILI9341_T4` at
`95be8e9487442230d9bcb45d23ffba72f6070c85` (based on upstream 1.7.0).
The normal product environments retain their existing dependency selection.

From the qualification workspace, build one environment at a time:

```powershell
ms build core --target teensy --env release_flash_candidate --stream
ms build core --target teensy --env dev_flash_candidate --stream
ms build core --target teensy --env hardware_flash_candidate --stream
```

Keep each ELF before starting the next workspace build: build preparation may
clean previous PlatformIO outputs. HEX artifacts alone cannot establish symbol
placement. Compare against the same application and upstream driver baseline.

The release candidate measured 1209820 bytes of Flash code against 1221892
for upstream, and 43008 bytes less total Flash. Its code advisory remains
1500 bytes over budget. Dev measured 1209868 bytes of code. Both retained free
RAM1/RAM2/PSRAM of 157024/336192/7165600 bytes. Neither proves display behavior.

The hardware candidate also built successfully: Flash code/data/headers
1210560/252992/8960 bytes; free RAM1/RAM2/PSRAM
155744/322496/7153184 bytes. Its benchmark-specific post-link checks passed,
with the diagnostics code advisory exceeded by 2240 bytes.

The driver repository includes a link-only fixture retaining overlays, touch
calibration and font data when referenced. These APIs must remain available;
the optimization changes garbage-collection granularity, not the public API.

Hardware qualification is pending. Follow the selected-controller procedure
in README.md, preserving live work and the restoration firmware first. Identify
the exact candidate HEX separately: inherited Manager profile names are not
sufficient to distinguish it from the normal benchmark image. The benchmark
HELLO currently times out on controller 18040250 / control port 8001.
Require a matching benchmark identity before running any scenarios. Validate
startup, DMA/VSync and display behavior physically before promoting the pin.
