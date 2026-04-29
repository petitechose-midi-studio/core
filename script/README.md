# Build Scripts

Utility scripts for asset conversion and build configuration.

## Scripts Overview

| Script | Purpose | When to Run |
|--------|---------|-------------|
| [lvgl/font/](lvgl/font/) | Convert TTF/OTF to LVGL binary fonts | Manual, when adding fonts |
| [lvgl/img/](lvgl/img/) | Convert PNG to LVGL C arrays | Manual, when adding images |

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

## Requirements

| Script | Dependencies |
|--------|--------------|
| Font converter | Node.js, `lv_font_conv` installed globally with `npm i -g lv_font_conv` |
| Image converter | Python 3.8+, [uv](https://github.com/astral-sh/uv) |

## Configuration Files

| File | Location | Purpose |
|------|----------|---------|
| `font_converter.conf` | `script/lvgl/font/` | Font paths, character ranges |
| `img_converter.conf` | `script/lvgl/img/` | Image paths, color format, compression |
