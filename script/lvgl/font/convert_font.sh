#!/bin/bash
# LVGL Font Converter - TTF/OTF to LVGL binary format with C arrays

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
CONFIG_FILE="$PROJECT_ROOT/script/lvgl/font/font_converter.conf"

# --- Colors ---
RED='\033[0;31m'  GREEN='\033[0;32m'  YELLOW='\033[1;33m'
BLUE='\033[0;34m' BOLD='\033[1m'      DIM='\033[2m'  NC='\033[0m'

# --- Helpers ---
log()     { echo -e "${BLUE}●${NC} $1"; }
success() { echo -e "  ${GREEN}✓${NC} $1"; }
error()   { echo -e "${RED}✗${NC} $1" >&2; exit 1; }

# --- Menu: Arrow-key selection ---
select_option() {
    local -n _result=$1
    local prompt=$2; shift 2
    local opts=("$@") n=${#opts[@]} sel=0

    tput civis 2>/dev/null || true
    trap 'tput cnorm 2>/dev/null || true' RETURN

    echo -e "\n${BOLD}${prompt}${NC}\n${DIM}↑↓/jk navigate, Enter select, q quit${NC}\n"

    draw() {
        for ((i=0; i<n; i++)); do
            ((i == sel)) && echo -e "  ${GREEN}▶${NC} ${BOLD}${opts[$i]}${NC}" \
                         || echo -e "    ${DIM}${opts[$i]}${NC}"
        done
    }

    draw
    while true; do
        IFS= read -rsn1 key
        case "$key" in
            $'\x1b')
                read -rsn2 -t 0.1 seq || true
                [[ $seq == '[A' || $seq == 'OA' ]] && ((sel > 0))     && ((sel--)) || true
                [[ $seq == '[B' || $seq == 'OB' ]] && ((sel < n-1))   && ((sel++)) || true ;;
            k|K) ((sel > 0))   && ((sel--)) || true ;;
            j|J) ((sel < n-1)) && ((sel++)) || true ;;
            '')  _result="${opts[$sel]}"; echo; return 0 ;;
            q|Q) echo; exit 0 ;;
        esac
        tput cuu "$n" 2>/dev/null || true
        draw
    done
}

# --- Input: Font size (4-100) ---
read_size() {
    local -n _size=$1
    echo -e "\n${BOLD}Font size (4-100):${NC}"
    while true; do
        echo -en "${YELLOW}Size: ${NC}"; read -r _size
        [[ $_size =~ ^[0-9]+$ ]] && ((_size >= 4 && _size <= 100)) && return 0
        echo -e "${RED}Invalid. Enter 4-100.${NC}"
    done
}

# --- Main ---
main() {
    echo -e "\n${BOLD}${BLUE}═══ LVGL Font Converter ═══${NC}"

    # Load config
    [[ -f $CONFIG_FILE ]] || error "Config not found: $CONFIG_FILE"
    source "$CONFIG_FILE"

    # Expand presets to hex ranges
    expand_range() {
        local input="${1:-ASCII,LATIN1,PUA}"
        local result=""
        IFS=',' read -ra parts <<< "$input"
        for part in "${parts[@]}"; do
            part=$(echo "$part" | tr -d ' ')
            case "$part" in
                ASCII)     part="0x20-0x7F" ;;
                LATIN1)    part="0x80-0xFF" ;;
                LATIN_EXT) part="0x100-0x24F" ;;
                SYMBOLS)   part="0x2000-0x206F,0x20A0-0x20CF" ;;
                PUA)       part="0xE000-0xF8FF" ;;
                UTF8)      part="0x20-0x7F,0x80-0xFF,0x100-0x24F" ;;
            esac
            [[ -n "$result" ]] && result+=","
            result+="$part"
        done
        echo "$result"
    }
    CHAR_RANGE="$(expand_range "${CHAR_RANGE:-ASCII,LATIN1,PUA}")"

    local src_dir="$PROJECT_ROOT/$FONT_SOURCE_DIR"
    local out_dir="$PROJECT_ROOT/$FONT_OUTPUT_DIR"

    [[ -d $src_dir ]] || error "Source not found: $FONT_SOURCE_DIR"
    echo -e "${DIM}Source: $src_dir\nOutput: $out_dir${NC}"

    # Find fonts
    local fonts=()
    while IFS= read -r -d '' f; do fonts+=("${f#$src_dir/}"); done \
        < <(find "$src_dir" -type f \( -name "*.ttf" -o -name "*.otf" \) -print0 | sort -z)
    ((${#fonts[@]} == 0)) && error "No fonts in $src_dir"

    # 1. Select font
    local font; select_option font "Select font:" "${fonts[@]}"
    local font_path="$src_dir/$font"
    local font_name; font_name=$(basename "$font" | sed 's/\.[^.]*$//' | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/_/g')
    success "Font: $font"

    # 2. Select size
    local size; read_size size
    success "Size: ${size}px"

    # 3. Select BPP
    local bpp_opts=("1 - Mono" "2 - 4 levels" "4 - 16 levels (recommended)" "8 - 256 levels")
    local bpp_sel; select_option bpp_sel "Bits per pixel:" "${bpp_opts[@]}"
    local bpp=${bpp_sel%% *}
    success "BPP: $bpp"

    # Confirm
    local out_name="${font_name}_${size}" arr_name="${font_name}_${size}_bin"
    echo -e "\n${BOLD}Output:${NC} ${out_name}.c.inc"
    echo -en "${YELLOW}Proceed? [Y/n]: ${NC}"; read -r confirm
    [[ $confirm =~ ^[Nn] ]] && { echo "Cancelled."; exit 0; }

    # Check dependencies
    command -v lv_font_conv &>/dev/null || error "lv_font_conv not found. Run: npm i -g lv_font_conv"

    # Convert to binary
    echo; log "Converting..."
    local data_dir="$out_dir/data" bin_file; bin_file=$(mktemp)
    mkdir -p "$data_dir"

    lv_font_conv --font "$font_path" --size "$size" --format bin --bpp "$bpp" \
                 --range "$CHAR_RANGE" --lv-font-name "$arr_name" --no-kerning -o "$bin_file"

    local bin_size; bin_size=$(stat -c%s "$bin_file" 2>/dev/null || stat -f%z "$bin_file")
    success "Binary: ${bin_size} bytes"

    # Generate C files
    log "Generating C files..."
    local cpp_file="$data_dir/${out_name}.c.inc"
    local hpp_file="$data_dir/${out_name}.hpp"
    local header="// Auto-generated | $(basename "$font") | ${size}px | ${bpp}bpp | $(date '+%Y-%m-%d %H:%M')"

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
    success "$(basename "$cpp_file")"
    success "$(basename "$hpp_file")"

    # Done
    echo -e "\n${GREEN}✓ Done!${NC}"
    echo -e "${DIM}#include \"data/${out_name}.hpp\""
    echo -e "lv_binfont_create_from_buffer(${arr_name}, ${arr_name}_len);${NC}"
}

main "$@"
