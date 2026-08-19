# Icon workbench

This directory is the isolated proving ground for the controller icon system.
Nothing here is consumed by the firmware build. The reviewed set was promoted
on 2026-08-18; production SVGs now live in `asset/icon`, while `reference`
retains the clean 512 px raster masters used for visual review. The unused
`CHORD_PROP_COLOR` master remains review-only, so the production registry has
70 glyphs.

The documentation registry is the single review manifest. It resolves to 71
review glyphs: the 72 documented references minus the retired `ACTION_APPLY`.
Production excludes the unused `CHORD_PROP_COLOR`. Seven families come from
the approved 1254 px proposal sheets; the simple `ACTION_CREATE` plus uses its
canonical transparent source.

## Build clean masters

```powershell
& ..\..\.venv\Scripts\python.exe `
  script\lvgl\icon\workbench\prepare_references.py write
```

This writes only inside the workbench:

- lossless source cells in `source-crops`;
- normalized transparent 512 px masters in `reference`;
- a compact overview in `reference-contact-sheet.png`;
- detailed family sheets in `reference-contact-sheets`.

## Trace and raster-check

Install the offline Potrace implementation once:

```powershell
uv pip install --python .venv/Scripts/python.exe potracer
```

Then generate the SVG candidates and exact-size proofs:

```powershell
& ..\..\.venv\Scripts\python.exe `
  script\lvgl\icon\workbench\trace_references.py write
```

Outputs:

- one review-only SVG per active icon in `traced/potrace`;
- transparent proofs at 512, 16, 14, and 12 px in `generated/potrace`;
- a complete atlas in `traced/potrace-contact-sheet.png`;
- detailed family comparisons in `traced/potrace-contact-sheets`;
- measured raster fidelity in `traced/potrace-quality.tsv`.

The checks require all 71 names, reject stale outputs, reject empty raster
proofs, enforce the 10% master safe area, and require at least 98% overlap
between each clean master and its traced rendering.

Validate an existing trace without rewriting it:

```powershell
& ..\..\.venv\Scripts\python.exe `
  script\lvgl\icon\workbench\trace_references.py check
```

## Production boundary

- `reference` is the review master set; generated proofs stay ignored.
- `asset/icon` is the only production SVG registry.
- `src/ui/font/StandaloneIcons.hpp` and the three LVGL blobs are generated,
  never hand-edited.
- After changing an approved production SVG, rebuild the font from the Core
  root with:

```powershell
& ..\..\.venv\Scripts\python.exe `
  ..\..\open-control\ui-lvgl-cli-tools\icon\build.py
```
