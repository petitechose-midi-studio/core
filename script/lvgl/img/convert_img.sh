#!/bin/bash
# LVGL Image Converter - PNG to LVGL C arrays

set -e

# --- Config ---
find_root() {
    local dir="$(pwd)"
    while [[ "$dir" != "/" ]]; do
        [[ -f "$dir/platformio.ini" ]] && echo "$dir" && return
        dir="$(dirname "$dir")"
    done
    echo "$(pwd)"
}
PROJECT_ROOT="$(find_root)"
CONFIG_FILE="$PROJECT_ROOT/script/lvgl/img/img_converter.conf"

# --- Colors ---
GREEN='\033[0;32m' BLUE='\033[0;34m' RED='\033[0;31m' DIM='\033[2m' NC='\033[0m'

log()     { echo -e "${BLUE}●${NC} $1"; }
success() { echo -e "  ${GREEN}✓${NC} $1"; }
error()   { echo -e "${RED}✗${NC} $1" >&2; exit 1; }

# --- Load config ---
[[ -f $CONFIG_FILE ]] || error "Config not found: $CONFIG_FILE"
source "$CONFIG_FILE"

SRC_DIR="$PROJECT_ROOT/$IMG_SOURCE_DIR"
OUT_DIR="$PROJECT_ROOT/$IMG_OUTPUT_DIR"

echo -e "\n${BLUE}═══ LVGL Image Converter ═══${NC}"
echo -e "${DIM}Source: $SRC_DIR${NC}"
echo -e "${DIM}Output: $OUT_DIR${NC}"
echo -e "${DIM}Format: $COLOR_FORMAT | Compress: $COMPRESS${NC}"

[[ -d $SRC_DIR ]] || error "Source not found: $IMG_SOURCE_DIR"

# --- Find images ---
shopt -s nullglob
images=("$SRC_DIR"/*.png "$SRC_DIR"/*.PNG)
shopt -u nullglob

((${#images[@]} == 0)) && error "No PNG images in $SRC_DIR"

echo -e "\n${DIM}Found ${#images[@]} image(s)${NC}\n"

# --- Export for Python ---
export IMG_OUTPUT_DIR="$OUT_DIR"
export COLOR_FORMAT
export COMPRESS

# --- Find converter script (in same dir as this script) ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONVERTER="$SCRIPT_DIR/img_converter.py"

[[ -f $CONVERTER ]] || error "Python converter not found: $CONVERTER"

# --- Convert all images ---
mkdir -p "$OUT_DIR"
converted=0
failed=0

for img in "${images[@]}"; do
    if uv run python "$CONVERTER" "$img"; then
        ((converted++))
    else
        ((failed++))
    fi
done

# --- Summary ---
echo ""
if ((failed == 0)); then
    echo -e "${GREEN}✓ Done! $converted image(s) converted${NC}"
else
    echo -e "${RED}✗ $failed error(s), $converted converted${NC}"
    exit 1
fi
