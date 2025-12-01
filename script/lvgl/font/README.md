# LVGL Font Converter

Converts TTF/OTF fonts to LVGL binary format for embedded use.

## Usage

```bash
bash script/lvgl/font/convert_font.sh
```

Interactive prompts guide you through:
1. Font selection
2. Size (4-100px)
3. Bits per pixel (1/2/4/8)
4. Confirmation

## Configuration

Create `script/lvgl/font/font_converter.conf` in your project:

```bash
FONT_SOURCE_DIR="asset/font"           # TTF/OTF source directory
FONT_OUTPUT_DIR="src/ui/shared/font"   # Output directory (data/ subfolder auto-created)
CHAR_RANGE="ASCII,LATIN1"              # Character range (see presets below)
```

## Character Range Presets

| Preset | Range | Description |
|--------|-------|-------------|
| `ASCII` | 0x20-0x7F | Basic Latin |
| `LATIN1` | 0x80-0xFF | Latin-1 Supplement (accents) |
| `LATIN_EXT` | 0x100-0x24F | Latin Extended A/B |
| `SYMBOLS` | 0x2000-0x206F, 0x20A0-0x20CF | Punctuation, Currency |
| `PUA` | 0xE000-0xF8FF | Private Use Area (icons) |
| `UTF8` | ASCII + LATIN1 + LATIN_EXT | Full text support |

### Examples

```bash
# Icon fonts only
CHAR_RANGE="PUA"

# Standard text
CHAR_RANGE="UTF8"

# Mix presets
CHAR_RANGE="ASCII,SYMBOLS,PUA"

# Custom hex ranges
CHAR_RANGE="0x20-0x7F,0xE000-0xE0FF"
```

## Output Files

For `myfont.ttf` at 14px:
- `data/myfont_14.c.inc` - Binary data array
- `data/myfont_14.hpp` - Header with extern declarations

## Integration

```cpp
#include "data/myfont_14.hpp"

// Load font
lv_font_t* font = lv_binfont_create_from_buffer(
    myfont_14_bin,
    myfont_14_bin_len
);
```

## Requirements

- Node.js (for lv_font_conv)
- `npm install -g lv_font_conv` (auto-installed if missing)
