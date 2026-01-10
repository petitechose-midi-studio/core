#!/bin/bash
# Build desktop simulator with Zig + Ninja (fast incremental builds)
set -e

# =============================================================================
# Colors & formatting
# =============================================================================
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; GRAY='\033[0;90m'; BOLD='\033[1m'; NC='\033[0m'
CHECKMARK="${GREEN}✓${NC}"; CROSS="${RED}✗${NC}"; ARROW="${CYAN}→${NC}"
HIDE_CURSOR='\033[?25l'; SHOW_CURSOR='\033[?25h'

# =============================================================================
# Spinner with timer
# =============================================================================
SPINNER_PID=""
START_TIME=""

spinner() {
    local chars="⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"
    while :; do
        local elapsed=$((SECONDS - START_TIME))
        for (( i=0; i<${#chars}; i++ )); do
            printf "\r  ${CYAN}%s${NC} %s ${GRAY}(%ds)${NC}" "${chars:$i:1}" "$1" "$elapsed"
            sleep 0.1
        done
    done
}

start_spinner() { printf "${HIDE_CURSOR}"; START_TIME=$SECONDS; spinner "$1" & SPINNER_PID=$!; }
stop_spinner() {
    [[ -n "$SPINNER_PID" ]] && kill "$SPINNER_PID" 2>/dev/null
    SPINNER_PID=""
    printf "\r\033[K${SHOW_CURSOR}"
}
trap 'printf "${SHOW_CURSOR}"; stop_spinner' EXIT

# =============================================================================
# Paths
# =============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# =============================================================================
# Initialize tools if needed
# =============================================================================
if [[ ! -f ".env" ]]; then
    echo -e "${YELLOW}First run - initializing tools...${NC}"
    ./init_tools.sh
    echo ""
fi

source ".env"

# Platform detection
case "$OSTYPE" in
    msys*|mingw*|cygwin*) EXE_EXT=".exe" ;;
    *) EXE_EXT="" ;;
esac

# Tool paths
CMAKE="$CMAKE_DIR/bin/cmake${EXE_EXT}"
NINJA="$NINJA_DIR/ninja${EXE_EXT}"
export PATH="$NINJA_DIR:$PATH"
export ZIG_DIR

# Verify tools exist
if [[ ! -x "$CMAKE" ]]; then
    echo -e "${RED}CMake not found. Run: ./init_tools.sh${NC}"
    exit 1
fi

# =============================================================================
# Configuration
# =============================================================================
BUILD_TYPE="${1:-Debug}"
BUILD_DIR="$SCRIPT_DIR/build"

echo ""
echo -e "${BOLD}Building Desktop Simulator${NC}"
echo -e "${GRAY}─────────────────────────────────────────${NC}"
echo -e "  ${ARROW} Type: ${CYAN}${BUILD_TYPE}${NC}"
echo -e "  ${ARROW} Compiler: ${CYAN}Zig${NC}"
echo -e "  ${ARROW} Generator: ${CYAN}Ninja${NC}"

# =============================================================================
# CMake Configure
# =============================================================================
mkdir -p "$BUILD_DIR"

NEED_CONFIGURE=false
[[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] && NEED_CONFIGURE=true

if $NEED_CONFIGURE; then
    start_spinner "Configuring CMake..."

    "$CMAKE" -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -G "Ninja" \
        -Wno-dev \
        > "$BUILD_DIR/cmake_configure.log" 2>&1 || {
        stop_spinner
        echo -e "  ${CROSS} CMake configure failed"
        echo -e "     ${GRAY}See: build/cmake_configure.log${NC}"
        tail -20 "$BUILD_DIR/cmake_configure.log"
        exit 1
    }

    stop_spinner
    echo -e "  ${CHECKMARK} CMake configured"
fi

# =============================================================================
# Build (parallel)
# =============================================================================
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
start_spinner "Compiling (${NPROC} threads)..."

"$CMAKE" --build "$BUILD_DIR" -j "$NPROC" > "$BUILD_DIR/build.log" 2>&1 || {
    stop_spinner
    echo -e "  ${CROSS} Build failed"
    echo -e "     ${GRAY}See: build/build.log${NC}"
    tail -30 "$BUILD_DIR/build.log"
    exit 1
}

stop_spinner
ELAPSED=$((SECONDS - START_TIME))
echo -e "  ${CHECKMARK} Build complete ${GRAY}(${ELAPSED}s)${NC}"

# =============================================================================
# Output
# =============================================================================
DEMO_EXE="$SCRIPT_DIR/bin/midi_studio_desktop${EXE_EXT}"

if [[ -f "$DEMO_EXE" ]]; then
    SIZE=$(du -h "$DEMO_EXE" | cut -f1)
    echo ""
    echo -e "${GREEN}Output:${NC} bin/midi_studio_desktop${EXE_EXT} ${GRAY}(${SIZE})${NC}"
fi

echo ""
