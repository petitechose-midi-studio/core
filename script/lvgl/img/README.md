# LVGL Image Converter

Converts PNG images to LVGL C arrays for embedded use.

## Usage

```bash
bash script/lvgl/img/convert_img.sh
```

Converts all PNG files in the source directory to C arrays.

## Configuration

Create `script/lvgl/img/img_converter.conf` in your project:

```bash
IMG_SOURCE_DIR="asset/img"        # PNG source directory
IMG_OUTPUT_DIR="src/ui/img"       # Output directory for .c files
COLOR_FORMAT="ARGB8565"           # Color format (see options below)
COMPRESS="LZ4"                    # Compression method
```

### Color Formats

| Format | Description |
|--------|-------------|
| `RGB565` | 16-bit, no alpha |
| `ARGB8888` | 32-bit with alpha |
| `ARGB8565` | 24-bit with alpha (default) |
| `RGB565A8` | 16-bit + separate alpha |
| `L8` | 8-bit grayscale |
| `A8` | 8-bit alpha only |

### Compression Methods

| Method | Description |
|--------|-------------|
| `NONE` | No compression |
| `LZ4` | Fast compression (default) |
| `RLE` | Run-length encoding |

## Output

For `myimage.png`:
- `src/ui/img/myimage.c` — C array with `PROGMEM` attribute

## Integration

```cpp
// Declare extern (generated in .c file)
extern const lv_img_dsc_t myimage;

// Use in LVGL
lv_img_set_src(img_widget, &myimage);
```

## Files

| File | Purpose |
|------|---------|
| `convert_img.sh` | Bash wrapper, processes all PNGs |
| `img_converter.py` | Python converter using LVGL library |
| `img_converter.conf` | Configuration file |

## Requirements

- Python 3.8+
- [uv](https://github.com/astral-sh/uv) (Python package manager)
- LVGL library in `.pio/libdeps/` (auto-installed by PlatformIO)
