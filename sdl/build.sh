#!/bin/bash
# MIDI Studio SDL Build
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR/tools"
CORE_DIR="$SCRIPT_DIR/.."
WORKSPACE_DIR="$(cd "$CORE_DIR/../.." && pwd)"

# App configuration (can be overridden via argument)
APP_PATH="$SCRIPT_DIR"
APP_ID="core"

# Versions
ZIG_VERSION="0.15.2"
NINJA_VERSION="v1.13.2"
SDL2_VERSION="2.32.10"
WATCHEXEC_VERSION="2.3.2"
EMSDK_VERSION="4.0.23"
EMSDK_REVISION="c0bb220cb6e6f4e0fabb6f6db9efd53390ef5e56"
LVGL_REVISION="85aa60d18b3d5e5588d7b247abf90198f07c8a63"

# ═══════════════════════════════════════════════════════════════════
# Colors & Logging
# ═══════════════════════════════════════════════════════════════════
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
BOLD='\033[1m'
DIM='\033[2m'
GRAY='\033[38;5;248m'
NC='\033[0m'

log()     { echo -e "${CYAN}●${NC} $1"; }
success() { echo -e "${GREEN}✓${NC} $1"; }
warn()    { echo -e "${YELLOW}⚠${NC} $1"; }
fail()    { echo -e "${RED}✗${NC} $1" >&2; exit 1; }

# Load app configuration from app.cmake
load_app_config() {
    local app_cmake="$APP_PATH/app.cmake"
    [[ -f "$app_cmake" ]] || fail "App config not found: $app_cmake"
    
    # Extract APP_ID from cmake file
    APP_ID=$(grep -E "^set\(APP_ID" "$app_cmake" | sed 's/.*"\([^"]*\)".*/\1/')
    [[ -n "$APP_ID" ]] || fail "APP_ID not found in $app_cmake"
    
    log "App: ${BOLD}$APP_ID${NC} ${DIM}($APP_PATH)${NC}"
}

# ═══════════════════════════════════════════════════════════════════
# Spinner & Progress
# ═══════════════════════════════════════════════════════════════════
SPIN='⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏'
BUILD_OUTPUT=""

draw_bar() {
    local pct=$1 width=${2:-20}
    local filled=$((pct * width / 100))
    local empty=$((width - filled))
    local bar=""
    for ((i=0; i<filled; i++)); do bar+="█"; done
    for ((i=0; i<empty; i++)); do bar+="░"; done
    echo "$bar"
}

hide_cursor() { tput civis 2>/dev/null || printf '\033[?25l'; }
show_cursor() { tput cnorm 2>/dev/null || printf '\033[?25h'; }

# Ensure cursor is restored on exit/interrupt
trap 'show_cursor' EXIT INT TERM

# Run command with spinner (no progress)
run_with_spinner() {
    local label="$1"; shift
    local start=$(date +%s)
    local logfile=$(mktemp)
    local spin_idx=0

    hide_cursor

    "$@" > "$logfile" 2>&1 &
    local pid=$!

    while kill -0 $pid 2>/dev/null; do
        local elapsed=$(($(date +%s) - start))
        local spin_char="${SPIN:$spin_idx:1}"
        printf "\r  ${GRAY}%s %s %ds${NC}   " "$label" "$spin_char" "$elapsed"
        spin_idx=$(( (spin_idx + 1) % 10 ))
        sleep 0.1
    done
    wait $pid
    local status=$?
    printf "\r                                        \r"
    show_cursor

    BUILD_OUTPUT="$(cat "$logfile")"
    rm -f "$logfile"
    return $status
}

# Run ninja with progress bar
run_ninja_with_progress() {
    local build_dir="$1"
    local start=$(date +%s)
    local logfile=$(mktemp)
    local jobs=$(nproc 2>/dev/null || echo 8)
    local spin_idx=0

    hide_cursor

    ninja -C "$build_dir" -j"$jobs" > "$logfile" 2>&1 &
    local pid=$!
    
    while kill -0 $pid 2>/dev/null; do
        local elapsed=$(($(date +%s) - start))
        local spin_char="${SPIN:$spin_idx:1}"
        spin_idx=$(( (spin_idx + 1) % 10 ))
        
        if [[ -f "$logfile" ]]; then
            local last_line=$(tail -1 "$logfile" 2>/dev/null)
            if [[ "$last_line" =~ ^\[([0-9]+)/([0-9]+)\] ]]; then
                local current="${BASH_REMATCH[1]}"
                local total="${BASH_REMATCH[2]}"
                local pct=$((current * 100 / total))
                printf "\r  ${GRAY}$(draw_bar $pct) %3d%% [%d/%d] %ds${NC}   " "$pct" "$current" "$total" "$elapsed"
            else
                printf "\r  ${GRAY}Compiling %s %ds${NC}   " "$spin_char" "$elapsed"
            fi
        else
            printf "\r  ${GRAY}Compiling %s %ds${NC}   " "$spin_char" "$elapsed"
        fi
        sleep 0.1
    done
    
    wait $pid
    local status=$?
    printf "\r                                                      \r"
    show_cursor

    BUILD_OUTPUT="$(cat "$logfile")"
    rm -f "$logfile"
    return $status
}

# ═══════════════════════════════════════════════════════════════════
# Interactive Menu (Arrow-key navigation)
# ═══════════════════════════════════════════════════════════════════
select_option() {
    local -n _result=$1
    local prompt=$2; shift 2
    local opts=("$@") n=${#opts[@]} sel=0

    tput civis 2>/dev/null || true
    trap 'tput cnorm 2>/dev/null || true' RETURN

    echo -e "\n${BOLD}${prompt}${NC}"
    echo -e "${DIM}↑↓ navigate, Enter select, q quit${NC}\n"

    draw() {
        for ((i=0; i<n; i++)); do
            if ((i == sel)); then
                echo -e "  ${GREEN}▶${NC} ${BOLD}${opts[$i]}${NC}"
            else
                echo -e "    ${DIM}${opts[$i]}${NC}"
            fi
        done
    }

    draw
    while true; do
        IFS= read -rsn1 key
        case "$key" in
            $'\x1b')
                read -rsn2 -t 0.1 seq || true
                [[ $seq == '[A' || $seq == 'OA' ]] && ((sel > 0))   && ((sel--)) || true
                [[ $seq == '[B' || $seq == 'OB' ]] && ((sel < n-1)) && ((sel++)) || true ;;
            k|K) ((sel > 0))   && ((sel--)) || true ;;
            j|J) ((sel < n-1)) && ((sel++)) || true ;;
            '')  _result="${opts[$sel]}"; echo; return 0 ;;
            q|Q) echo; exit 0 ;;
        esac
        tput cuu "$n" 2>/dev/null || printf '\033[%dA' "$n"
        draw
    done
}

# ═══════════════════════════════════════════════════════════════════
# Tools Setup
# ═══════════════════════════════════════════════════════════════════
setup_ninja() {
    if command -v ninja &>/dev/null || [[ -f "$TOOLS_DIR/ninja/ninja.exe" ]]; then
        [[ -f "$TOOLS_DIR/ninja/ninja.exe" ]] && export PATH="$TOOLS_DIR/ninja:$PATH"
        return 0
    fi
    log "Downloading Ninja ${DIM}$NINJA_VERSION${NC}"
    mkdir -p "$TOOLS_DIR/ninja"
    curl -sSL "https://github.com/ninja-build/ninja/releases/download/$NINJA_VERSION/ninja-win.zip" -o "$TOOLS_DIR/ninja.zip"
    unzip -q "$TOOLS_DIR/ninja.zip" -d "$TOOLS_DIR/ninja" && rm -f "$TOOLS_DIR/ninja.zip"
    export PATH="$TOOLS_DIR/ninja:$PATH"
}

setup_lvgl() {
    local lvgl_dir="$TOOLS_DIR/lvgl"
    if [[ ! -d "$lvgl_dir/.git" ]]; then
        log "Cloning LVGL ${DIM}$LVGL_REVISION${NC}"
        git clone --filter=blob:none --no-checkout https://github.com/lvgl/lvgl.git "$lvgl_dir"
    fi
    if [[ -n "$(git -C "$lvgl_dir" status --porcelain)" ]]; then
        fail "LVGL tool checkout is dirty: $lvgl_dir"
    fi
    if [[ "$(git -C "$lvgl_dir" rev-parse HEAD 2>/dev/null || true)" != "$LVGL_REVISION" ]]; then
        git -C "$lvgl_dir" fetch --depth 1 origin "$LVGL_REVISION"
        git -C "$lvgl_dir" checkout --detach "$LVGL_REVISION"
    fi
}

set_dependency_cmake_args() {
    setup_lvgl
    DEPENDENCY_CMAKE_ARGS=(
        -DOPEN_CONTROL_FRAMEWORK_DIR="$WORKSPACE_DIR/open-control/framework"
        -DOPEN_CONTROL_UI_LVGL_DIR="$WORKSPACE_DIR/open-control/ui-lvgl"
        -DOPEN_CONTROL_UI_COMPONENTS_DIR="$WORKSPACE_DIR/open-control/ui-lvgl-components"
        -DOPEN_CONTROL_HAL_SDL_DIR="$WORKSPACE_DIR/open-control/hal-sdl"
        -DOPEN_CONTROL_HAL_NET_DIR="$WORKSPACE_DIR/open-control/hal-net"
        -DOPEN_CONTROL_HAL_MIDI_DIR="$WORKSPACE_DIR/open-control/hal-midi"
        -DOPEN_CONTROL_NOTE_DIR="$WORKSPACE_DIR/open-control/note"
        -DMIDI_STUDIO_UI_DIR="$WORKSPACE_DIR/midi-studio/ui"
        -DLVGL_DIR="$TOOLS_DIR/lvgl"
    )
}

setup_native_tools() {
    mkdir -p "$TOOLS_DIR"
    if [[ ! -f "$TOOLS_DIR/zig/zig.exe" ]]; then
        log "Downloading Zig ${DIM}$ZIG_VERSION${NC}"
        curl -sSL "https://ziglang.org/download/$ZIG_VERSION/zig-x86_64-windows-$ZIG_VERSION.zip" -o "$TOOLS_DIR/zig.zip"
        unzip -q "$TOOLS_DIR/zig.zip" -d "$TOOLS_DIR" && mv "$TOOLS_DIR/zig-x86_64-windows-$ZIG_VERSION" "$TOOLS_DIR/zig"
        rm -f "$TOOLS_DIR/zig.zip"
    fi
    [[ -f "$TOOLS_DIR/zig-cc.cmd" ]] || echo -e '@echo off\n"%~dp0zig\\zig.exe" cc %*' > "$TOOLS_DIR/zig-cc.cmd"
    [[ -f "$TOOLS_DIR/zig-cxx.cmd" ]] || echo -e '@echo off\n"%~dp0zig\\zig.exe" c++ %*' > "$TOOLS_DIR/zig-cxx.cmd"
    [[ -f "$TOOLS_DIR/zig-ar.cmd" ]] || echo -e '@echo off\n"%~dp0zig\\zig.exe" ar %*' > "$TOOLS_DIR/zig-ar.cmd"
    
    setup_ninja
    
    if [[ ! -f "$TOOLS_DIR/SDL2/include/SDL2/SDL.h" ]]; then
        log "Downloading SDL2 ${DIM}$SDL2_VERSION${NC}"
        curl -sSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-devel-$SDL2_VERSION-mingw.zip" -o "$TOOLS_DIR/sdl2.zip"
        unzip -q "$TOOLS_DIR/sdl2.zip" -d "$TOOLS_DIR"
        mv "$TOOLS_DIR/SDL2-$SDL2_VERSION/x86_64-w64-mingw32" "$TOOLS_DIR/SDL2"
        rm -rf "$TOOLS_DIR/SDL2-$SDL2_VERSION" "$TOOLS_DIR/sdl2.zip"
    fi
    set_dependency_cmake_args
    return 0
}

setup_wasm_tools() {
    local EMSDK="$TOOLS_DIR/emsdk"
    if [[ ! -d "$EMSDK/.git" ]]; then
        log "Cloning emsdk ${DIM}$EMSDK_REVISION${NC}"
        git clone --filter=blob:none --no-checkout https://github.com/emscripten-core/emsdk.git "$EMSDK"
    fi
    if [[ -n "$(git -C "$EMSDK" status --porcelain)" ]]; then
        fail "emsdk tool checkout is dirty: $EMSDK"
    fi
    if [[ "$(git -C "$EMSDK" rev-parse HEAD 2>/dev/null || true)" != "$EMSDK_REVISION" ]]; then
        git -C "$EMSDK" fetch --depth 1 origin "$EMSDK_REVISION"
        git -C "$EMSDK" checkout --detach "$EMSDK_REVISION"
    fi
    log "Activating Emscripten ${DIM}$EMSDK_VERSION${NC}"
    "$EMSDK/emsdk.bat" install "$EMSDK_VERSION"
    "$EMSDK/emsdk.bat" activate "$EMSDK_VERSION"
    # shellcheck disable=SC1091
    source "$EMSDK/emsdk_env.sh" >/dev/null
    setup_ninja
    set_dependency_cmake_args
    return 0
}

setup_watchexec() {
    [[ -f "$TOOLS_DIR/watchexec/watchexec.exe" ]] && return 0
    log "Downloading watchexec ${DIM}$WATCHEXEC_VERSION${NC}"
    mkdir -p "$TOOLS_DIR/watchexec"
    curl -sSL "https://github.com/watchexec/watchexec/releases/download/v$WATCHEXEC_VERSION/watchexec-$WATCHEXEC_VERSION-x86_64-pc-windows-msvc.zip" -o "$TOOLS_DIR/watchexec.zip"
    unzip -q "$TOOLS_DIR/watchexec.zip" -d "$TOOLS_DIR/watchexec"
    mv "$TOOLS_DIR/watchexec/watchexec-$WATCHEXEC_VERSION-x86_64-pc-windows-msvc/watchexec.exe" "$TOOLS_DIR/watchexec/"
    rm -rf "$TOOLS_DIR/watchexec/watchexec-$WATCHEXEC_VERSION-x86_64-pc-windows-msvc" "$TOOLS_DIR/watchexec.zip"
}

# ═══════════════════════════════════════════════════════════════════
# Build
# ═══════════════════════════════════════════════════════════════════
do_build_native() {
    load_app_config
    local BUILD_DIR="$SCRIPT_DIR/build/$APP_ID/native"
    local BIN_DIR="$SCRIPT_DIR/bin/$APP_ID/native"
    local start=$(date +%s)
    echo -e "\n${BOLD}${BLUE}═══ Native Build: $APP_ID ═══${NC}\n"
    
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        setup_native_tools
        mkdir -p "$BUILD_DIR"
        if ! run_with_spinner "Configuring" cmake -G Ninja -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/zig-toolchain.cmake" \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DSDL2_ROOT="$TOOLS_DIR/SDL2" \
            -DBIN_OUTPUT_DIR="$SCRIPT_DIR/bin" \
            -DAPP_PATH="$APP_PATH" \
            "${DEPENDENCY_CMAKE_ARGS[@]}"; then
            echo "$BUILD_OUTPUT"
            fail "CMake configuration failed"
        fi
    fi
    
    if ! run_ninja_with_progress "$BUILD_DIR"; then
        echo "$BUILD_OUTPUT" | grep -E "(error|Error)" | head -10
        fail "Build failed"
    fi
    
    python3 "$CORE_DIR/script/pio/merge_compiledb.py" "$CORE_DIR" 2>/dev/null || true
    local elapsed=$(($(date +%s) - start))
    local exe_name=$(ls "$BIN_DIR"/*.exe 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo "midi_studio_$APP_ID.exe")
    success "bin/$APP_ID/native/$exe_name ${DIM}(${elapsed}s)${NC}"
}

do_build_wasm() {
    load_app_config
    local BUILD_DIR="$SCRIPT_DIR/build/$APP_ID/wasm"
    local BIN_DIR="$SCRIPT_DIR/bin/$APP_ID/wasm"
    local EMSDK="$TOOLS_DIR/emsdk"
    local start=$(date +%s)
    echo -e "\n${BOLD}${BLUE}═══ WASM Build: $APP_ID ═══${NC}\n"
    
    setup_wasm_tools
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        mkdir -p "$BUILD_DIR" && rm -rf "$BUILD_DIR"/*
        cd "$BUILD_DIR"
        if ! run_with_spinner "Configuring" python "$EMSDK/upstream/emscripten/emcmake.py" cmake "$SCRIPT_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DBIN_OUTPUT_DIR="$SCRIPT_DIR/bin" \
            -DAPP_PATH="$APP_PATH" \
            "${DEPENDENCY_CMAKE_ARGS[@]}"; then
            echo "$BUILD_OUTPUT"
            fail "CMake configuration failed"
        fi
    fi
    
    cd "$BUILD_DIR"
    if ! run_ninja_with_progress "$BUILD_DIR"; then
        echo "$BUILD_OUTPUT" | grep -E "(error|Error)" | head -10
        fail "Build failed"
    fi
    
    local elapsed=$(($(date +%s) - start))
    local wasm_file=$(ls "$BIN_DIR"/*.wasm 2>/dev/null | head -1)
    local size=$(du -h "$wasm_file" 2>/dev/null | cut -f1 || echo "?")
    local html_name=$(basename "$wasm_file" .wasm).html
    success "bin/$APP_ID/wasm/$html_name ${DIM}($size, ${elapsed}s)${NC}"
}

# ═══════════════════════════════════════════════════════════════════
# Run / Serve
# ═══════════════════════════════════════════════════════════════════
do_run() {
    do_build_native
    local exe=$(ls "$SCRIPT_DIR/bin/$APP_ID/native"/*.exe 2>/dev/null | head -1)
    [[ -f "$exe" ]] || fail "Executable not found"
    echo -e "\n${CYAN}●${NC} Running $APP_ID...\n"
    "$exe"
}

do_serve() {
    do_build_wasm
    local html=$(ls "$SCRIPT_DIR/bin/$APP_ID/wasm"/*.html 2>/dev/null | head -1)
    local html_name=$(basename "$html")
    echo -e "\n${CYAN}●${NC} Serving at ${BOLD}http://localhost:8000/$html_name${NC}\n"
    python -m http.server 8000 -d "$SCRIPT_DIR/bin/$APP_ID/wasm"
}

# ═══════════════════════════════════════════════════════════════════
# Watch
# ═══════════════════════════════════════════════════════════════════
do_watch_native() {
    setup_watchexec
    do_build_native
    echo -e "\n${CYAN}●${NC} Watching $APP_ID... ${DIM}(Ctrl+C to stop)${NC}"
    echo -e "${DIM}   File change → rebuild → restart${NC}\n"
    
    # Convert to Windows paths for PowerShell
    local build_dir_win=$(cygpath -w "$SCRIPT_DIR/build/$APP_ID/native")
    local exe=$(ls "$SCRIPT_DIR/bin/$APP_ID/native"/*.exe 2>/dev/null | head -1)
    local exe_win=$(cygpath -w "$exe")
    
    "$TOOLS_DIR/watchexec/watchexec.exe" \
        -w "$CORE_DIR/src" -w "$SCRIPT_DIR" -w "$APP_PATH" \
        -e cpp,hpp,h,c \
        --restart \
        --shell=powershell \
        -- "ninja -C '$build_dir_win'; if (\$LASTEXITCODE -eq 0) { & '$exe_win' }"
}

do_watch_wasm() {
    local EMSDK="$TOOLS_DIR/emsdk"
    do_build_wasm
    local html=$(ls "$SCRIPT_DIR/bin/$APP_ID/wasm"/*.html 2>/dev/null | head -1)
    echo -e "\n${CYAN}●${NC} Serving at ${BOLD}http://localhost:8000/$(basename "$html")${NC}"
    echo -e "${DIM}   emrun serves the WASM app${NC}\n"
    python "$EMSDK/upstream/emscripten/emrun.py" --port 8000 --no_browser "$html"
}

# ═══════════════════════════════════════════════════════════════════
# Operations
# ═══════════════════════════════════════════════════════════════════
run_op() {
    local op="$1" target="$2"
    case "$op" in
        build)
            if [[ "$target" == "native" ]]; then do_build_native; else do_build_wasm; fi ;;
        run)   do_run ;;
        serve) do_serve ;;
        watch)
            if [[ "$target" == "native" ]]; then do_watch_native; else do_watch_wasm; fi ;;
        clean)
            load_app_config
            rm -rf "$SCRIPT_DIR/build/$APP_ID" "$SCRIPT_DIR/bin/$APP_ID" 2>/dev/null
            success "Cleaned build/$APP_ID/ and bin/$APP_ID/"
            ;;
    esac
}

# ═══════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════
is_op()     { [[ "$1" =~ ^(build|run|serve|watch|clean)$ ]]; }
is_target() { [[ "$1" =~ ^(native|wasm)$ ]]; }

interactive_menu() {
    echo -e "\n${BOLD}${BLUE}═══ MIDI Studio SDL ═══${NC}"
    
    local op target=""
    select_option op "Select operation:" "build" "run" "serve" "watch" "clean"
    
    if [[ "$op" =~ ^(build|watch)$ ]]; then
        select_option target "Select target:" "native" "wasm"
    fi
    run_op "$op" "$target"
}

ask_op() {
    local _op
    select_option _op "Select operation:" "build" "run" "serve" "watch" "clean"
    OP="$_op"
}

ask_target() {
    local _target
    select_option _target "Select target:" "native" "wasm"
    TARGET="$_target"
}

# ═══════════════════════════════════════════════════════════════════
# Argument Parsing
# ═══════════════════════════════════════════════════════════════════
# Usage: build.sh <target> [app_path]
#        build.sh <operation> <target> [app_path]
#
# Examples:
#   build.sh native                     # Build core native
#   build.sh native /path/to/app/sdl    # Build custom app native
#   build.sh build native               # Same as above
#   build.sh clean /path/to/app/sdl     # Clean custom app

ARG1="${1:-}"
ARG2="${2:-}"
ARG3="${3:-}"

# Set APP_PATH if provided as last argument
set_app_path() {
    local path="$1"
    if [[ -n "$path" && -d "$path" && -f "$path/app.cmake" ]]; then
        APP_PATH="$(cd "$path" && pwd)"
    elif [[ -n "$path" && "$path" != "native" && "$path" != "wasm" ]]; then
        fail "Invalid app path: $path (must contain app.cmake)"
    fi
}

if [[ -z "$ARG1" ]]; then
    interactive_menu
elif is_target "$ARG1"; then
    # build.sh native [app_path]
    TARGET="$ARG1"
    OP="build"
    set_app_path "$ARG2"
    run_op "$OP" "$TARGET"
elif is_op "$ARG1"; then
    OP="$ARG1"
    if [[ "$OP" =~ ^(build|watch)$ ]]; then
        if is_target "$ARG2"; then
            TARGET="$ARG2"
            set_app_path "$ARG3"
        elif [[ -n "$ARG2" ]]; then
            # build.sh build /path → assume native
            TARGET="native"
            set_app_path "$ARG2"
        else
            ask_target
        fi
    elif [[ "$OP" == "clean" ]]; then
        set_app_path "$ARG2"
    fi
    run_op "$OP" "$TARGET"
else
    echo -e "${BOLD}Usage:${NC} $0 <target> [app_path]"
    echo -e "       $0 <operation> <target> [app_path]"
    echo ""
    echo -e "${DIM}Targets:${NC}    native, wasm"
    echo -e "${DIM}Operations:${NC} build, run, serve, watch, clean"
    echo ""
    echo -e "${DIM}Examples:${NC}"
    echo "  $0 native                      # Build core for desktop"
    echo "  $0 native ../plugin-bitwig/sdl # Build bitwig for desktop"
    echo "  $0 wasm ../plugin-bitwig/sdl   # Build bitwig for web"
fi
