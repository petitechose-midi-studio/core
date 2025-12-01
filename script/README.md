# Build Scripts

Utility scripts for asset conversion and build configuration.

## Scripts Overview

| Script | Purpose | When to Run |
|--------|---------|-------------|
| [lvgl/font/](lvgl/font/) | Convert TTF/OTF to LVGL binary fonts | Manual, when adding fonts |
| [lvgl/img/](lvgl/img/) | Convert PNG to LVGL C arrays | Manual, when adding images |
| [midi/sysex/](midi/sysex/) | Patch USB MIDI SysEx buffer size | Automatic (pre-build) |

## Usage

### Font Conversion

```bash
bash script/lvgl/font/convert_font.sh
```

Interactive prompts for font selection, size, and BPP.

### Image Conversion

```bash
bash script/lvgl/img/convert_img.sh
```

Batch converts all PNG files from configured source directory.

### SysEx Patch (Automatic)

Configured in `platformio.ini`:

```ini
extra_scripts =
    pre:script/midi/sysex/patch_usb_midi_sysex.py
```

Runs automatically before each build.

## Requirements

| Script | Dependencies |
|--------|--------------|
| Font converter | Node.js, `lv_font_conv` (auto-installed) |
| Image converter | Python 3.8+, [uv](https://github.com/astral-sh/uv) |
| SysEx patcher | Python 3 (via PlatformIO) |

## Configuration Files

| File | Location | Purpose |
|------|----------|---------|
| `font_converter.conf` | `script/lvgl/font/` | Font paths, character ranges |
| `img_converter.conf` | `script/lvgl/img/` | Image paths, color format, compression |
| `System.hpp` | `src/config/` | USB_SYSEX_MAX_SIZE for SysEx patcher |
