#!/bin/bash
# Watch source files and auto-rebuild + rerun on changes
# Usage: ./watch.sh [Debug|Release]
set -e

# =============================================================================
# Colors
# =============================================================================
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; GRAY='\033[0;90m'; BOLD='\033[1m'; NC='\033[0m'

# =============================================================================
# Paths
# =============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Debug}"
CORE_ROOT="$SCRIPT_DIR/.."
OPEN_CONTROL_ROOT="$SCRIPT_DIR/../../../open-control"

# Paths to watch for changes
WATCH_PATHS=(
    "$CORE_ROOT/src"
    "$CORE_ROOT/config"
    "$SCRIPT_DIR"
    "$OPEN_CONTROL_ROOT/framework/src"
    "$OPEN_CONTROL_ROOT/ui-lvgl/src"
    "$OPEN_CONTROL_ROOT/hal-sdl/src"
    "$OPEN_CONTROL_ROOT/ui-lvgl-components/src"
)

# Executable
case "$OSTYPE" in
    msys*|mingw*|cygwin*)
        DEMO_EXE="$SCRIPT_DIR/bin/midi_studio_desktop.exe"
        IS_WINDOWS=true
        ;;
    *)
        DEMO_EXE="$SCRIPT_DIR/bin/midi_studio_desktop"
        IS_WINDOWS=false
        ;;
esac

DEMO_PID=""

# =============================================================================
# Cleanup on exit
# =============================================================================
cleanup() {
    echo -e "\n${YELLOW}Stopping...${NC}"
    kill_demo
    exit 0
}
trap cleanup SIGINT SIGTERM

# =============================================================================
# Kill running demo
# =============================================================================
kill_demo() {
    if [[ -n "$DEMO_PID" ]]; then
        kill "$DEMO_PID" 2>/dev/null || true
        wait "$DEMO_PID" 2>/dev/null || true
        DEMO_PID=""
    fi
    # Also kill any orphan processes
    if $IS_WINDOWS; then
        taskkill //F //IM midi_studio_desktop.exe 2>/dev/null || true
    else
        pkill -f "midi_studio_desktop" 2>/dev/null || true
    fi
}

# =============================================================================
# Build and run
# =============================================================================
build_and_run() {
    echo ""
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BOLD}Change detected - Rebuilding...${NC}"

    kill_demo

    if ./build.sh "$BUILD_TYPE"; then
        echo -e "${GREEN}▶ Starting app...${NC}"
        "$DEMO_EXE" &
        DEMO_PID=$!
        echo -e "${GRAY}PID: $DEMO_PID${NC}"
    else
        echo -e "${RED}Build failed!${NC}"
    fi

    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GRAY}Watching for changes... (Ctrl+C to stop)${NC}"
}

# =============================================================================
# Header
# =============================================================================
echo ""
echo -e "${BOLD}Watch Mode${NC} - Auto rebuild + rerun"
echo -e "${GRAY}─────────────────────────────────────────${NC}"
echo -e "  Build: ${CYAN}$BUILD_TYPE${NC}"
echo -e "  Watching:"
for p in "${WATCH_PATHS[@]}"; do
    # Show relative path
    rel=$(realpath --relative-to="$SCRIPT_DIR" "$p" 2>/dev/null || echo "$p")
    echo -e "    ${GRAY}$rel${NC}"
done
echo ""

# =============================================================================
# Check for watchexec (best option)
# =============================================================================
USE_WATCHEXEC=false
if command -v watchexec &> /dev/null; then
    # Test if watchexec actually works (permission issues on some Windows setups)
    if watchexec --version &> /dev/null; then
        USE_WATCHEXEC=true
    fi
fi

if $USE_WATCHEXEC; then
    echo -e "${GREEN}Using watchexec${NC} (fast file watcher)"
    echo ""

    # Build watch arguments
    WATCH_ARGS=""
    for p in "${WATCH_PATHS[@]}"; do
        [[ -d "$p" ]] && WATCH_ARGS="$WATCH_ARGS --watch $p"
    done

    # watchexec: restart process on changes
    watchexec \
        --restart \
        --stop-signal SIGKILL \
        --exts "cpp,hpp,c,h" \
        $WATCH_ARGS \
        --debounce 300 \
        -- bash -c "./build.sh $BUILD_TYPE && $DEMO_EXE"

else
    # =============================================================================
    # Fallback: Simple polling (works everywhere)
    # =============================================================================
    echo -e "${YELLOW}watchexec not found${NC} - using polling (2s interval)"
    echo -e "${GRAY}Install for better perf:${NC}"
    echo -e "${GRAY}  Windows: winget install watchexec${NC}"
    echo -e "${GRAY}  Linux:   cargo install watchexec-cli${NC}"
    echo ""

    POLL_INTERVAL=2

    # Get checksum of source files
    get_checksum() {
        local paths=()
        for p in "${WATCH_PATHS[@]}"; do
            [[ -d "$p" ]] && paths+=("$p")
        done
        find "${paths[@]}" \( -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" \) 2>/dev/null | \
            xargs stat --format="%Y" 2>/dev/null | sort | md5sum | cut -d' ' -f1
    }

    LAST_CHECKSUM=$(get_checksum)

    # Initial build and run
    build_and_run

    # Poll loop
    while true; do
        sleep $POLL_INTERVAL

        CURRENT_CHECKSUM=$(get_checksum)

        if [[ "$CURRENT_CHECKSUM" != "$LAST_CHECKSUM" ]]; then
            LAST_CHECKSUM="$CURRENT_CHECKSUM"
            build_and_run
        fi
    done
fi
