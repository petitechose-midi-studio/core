#!/bin/bash
# Icon Font Builder - SVG to TTF with incremental builds

set -e

# --- Find project root ---
find_root() {
    local dir="$(pwd)"
    while [[ "$dir" != "/" ]]; do
        [[ -f "$dir/platformio.ini" ]] && echo "$dir" && return
        dir="$(dirname "$dir")"
    done
    echo "$(pwd)"
}

PROJECT_ROOT="$(find_root)"
SCRIPT_DIR="$PROJECT_ROOT/script/lvgl/icon"

# --- Colors ---
RED='\033[0;31m'    GREEN='\033[0;32m'  YELLOW='\033[1;33m'
BLUE='\033[0;34m'   BOLD='\033[1m'      DIM='\033[2m'  NC='\033[0m'

# --- Helpers ---
error() { echo -e "${RED}✗${NC} $1" >&2; exit 1; }

# --- Check dependencies ---
check_deps() {
    local missing=()

    [[ -f "$SCRIPT_DIR/build.py" ]] || missing+=("build.py not found")
    [[ -f "$SCRIPT_DIR/config.py" ]] || missing+=("config.py not found")
    command -v python &>/dev/null || missing+=("python")

    if ((${#missing[@]} > 0)); then
        echo -e "\n${BOLD}${RED}Missing dependencies:${NC}"
        for dep in "${missing[@]}"; do
            echo -e "  ${RED}✗${NC} $dep"
        done
        echo
        exit 1
    fi
}

# --- Main ---
main() {
    # Check deps
    check_deps

    # Run Python script
    cd "$PROJECT_ROOT"
    python "$SCRIPT_DIR/build.py" "$@"
}

main "$@"
