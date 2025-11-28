#!/bin/bash
# Batch font generation for core library
# Generates all fonts used by FontLoader.cpp

set -e

# --- Config ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

FONT_SOURCE_DIR="$PROJECT_ROOT/asset/font"
FONT_OUTPUT_DIR="$PROJECT_ROOT/src/ui/shared/font/data"

CHAR_RANGE="0x20-0x7F,0x80-0xFF"  # ASCII + Latin1
BPP=4

# --- Colors ---
GREEN='\033[0;32m'
BLUE='\033[0;34m'
DIM='\033[2m'
NC='\033[0m'

# --- Font list: "path:size" ---
FONTS=(
    "InterDisplay/InterDisplay-Bold.ttf:13"
    "InterDisplay/InterDisplay-Bold.ttf:14"
    "InterDisplay/InterDisplay-Bold.ttf:20"
    "InterDisplay/InterDisplay-Light.ttf:14"
    "InterDisplay/InterDisplay-Medium.ttf:13"
    "InterDisplay/InterDisplay-Medium.ttf:14"
    "InterDisplay/InterDisplay-Regular.ttf:14"
    "InterDisplay/InterDisplay-SemiBold.ttf:14"
    "JetBrainsMonoNL/JetBrainsMonoNL-Medium.ttf:13"
)

# --- Main ---
echo -e "\n${BLUE}═══ Batch Font Generator ═══${NC}"
echo -e "${DIM}Range: ASCII + Latin1 | BPP: $BPP${NC}\n"

# Check dependencies
command -v lv_font_conv &>/dev/null || { echo "Error: lv_font_conv not found. Run: npm i -g lv_font_conv"; exit 1; }

mkdir -p "$FONT_OUTPUT_DIR"

for entry in "${FONTS[@]}"; do
    font_path="${entry%:*}"
    size="${entry#*:}"

    full_path="$FONT_SOURCE_DIR/$font_path"
    font_name=$(basename "$font_path" | sed 's/\.[^.]*$//' | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/_/g')
    out_name="${font_name}_${size}"
    arr_name="${out_name}_bin"

    echo -en "${BLUE}●${NC} $out_name... "

    # Convert to binary
    bin_file=$(mktemp)
    lv_font_conv --font "$full_path" --size "$size" --format bin --bpp "$BPP" \
                 --range "$CHAR_RANGE" --lv-font-name "$arr_name" --no-kerning -o "$bin_file"

    bin_size=$(stat -c%s "$bin_file" 2>/dev/null || stat -f%z "$bin_file")

    # Generate C files
    cpp_file="$FONT_OUTPUT_DIR/${out_name}.c.inc"
    hpp_file="$FONT_OUTPUT_DIR/${out_name}.hpp"
    header="// Auto-generated | $(basename "$font_path") | ${size}px | ${BPP}bpp | $(date '+%Y-%m-%d %H:%M')"

    {
        echo "$header"; echo
        xxd -i "$bin_file" \
            | sed "s/unsigned char [^=]*=/const uint8_t ${arr_name}[] PROGMEM =/" \
            | sed "s/unsigned int [^;]*;/const uint32_t ${arr_name}_len = ${bin_size};/"
    } > "$cpp_file"

    cat > "$hpp_file" << EOF
$header
#pragma once
#include <Arduino.h>
extern const uint8_t ${arr_name}[] PROGMEM;
extern const uint32_t ${arr_name}_len;
EOF

    rm -f "$bin_file"
    echo -e "${GREEN}✓${NC} ${DIM}${bin_size} bytes${NC}"
done

echo -e "\n${GREEN}✓ Done!${NC} Generated ${#FONTS[@]} fonts in $FONT_OUTPUT_DIR"
