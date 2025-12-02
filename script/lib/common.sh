#!/bin/bash
# Common utilities for all scripts
# Source this file: source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

set -e

# --- Find project root (walks up until platformio.ini) ---
find_root() {
    local dir="$(pwd)"
    while [[ "$dir" != "/" && "$dir" != "." ]]; do
        [[ -f "$dir/platformio.ini" ]] && echo "$dir" && return
        dir="$(dirname "$dir")"
    done
    # Fallback: try from script location
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[1]:-${BASH_SOURCE[0]}}")" && pwd)"
    dir="$script_dir"
    while [[ "$dir" != "/" && "$dir" != "." ]]; do
        [[ -f "$dir/platformio.ini" ]] && echo "$dir" && return
        dir="$(dirname "$dir")"
    done
    echo "$(pwd)"
}

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

# --- Logging ---
log()     { echo -e "${BLUE}●${NC} $1"; }
success() { echo -e "${GREEN}✓${NC} $1"; }
warn()    { echo -e "${YELLOW}⚠${NC} $1"; }
error()   { echo -e "${RED}✗${NC} $1" >&2; exit 1; }

# --- Require command ---
require_cmd() {
    command -v "$1" &>/dev/null || error "$1 not found in PATH"
}
