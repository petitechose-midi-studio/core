# Icon Font Builder Configuration - Standalone Core

# Paths (relative to project root)
SVG_SOURCE_DIR = "asset/icon"
TTF_OUTPUT_DIR = "asset/font"
HEADER_OUTPUT_DIR = "src/ui/font"
CACHE_DIR = ".cache/icons"

# Tools (Windows)
INKSCAPE = "C:/Program Files/Inkscape/bin/inkscape.exe"
FONTFORGE = "C:/Program Files/FontForgeBuilds/bin/fontforge.exe"

# Font settings
FONT_NAME = "standalone_icons"
FONT_FAMILY = "Standalone Icons"
UNITS_PER_EM = 1000
ASCENT = 800
DESCENT = 200
GLYPH_MARGIN = 50
UNICODE_START = 0xE000

# SVG processing
PADDING_PERCENT = 0.10

# LVGL font generation
FONT_SIZES = {"S": 12, "M": 14, "L": 16}
LVGL_BPP = 4

# Header include (file that declares standalone_fonts global)
HEADER_INCLUDE = "StandaloneFonts.hpp"

# Name of the fonts struct instance (for icons::set)
FONTS_STRUCT = "standalone_fonts"

# Platform compatibility header (for cross-platform builds)
PLATFORM_INCLUDE = "config/PlatformCompat.hpp"

# Generated header
NAMESPACE = "standalone::icons"
HEADER_FILENAME = "StandaloneIcons.hpp"
