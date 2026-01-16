#!/bin/bash
# MIDI Studio SDL Build
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR/tools"
CORE_DIR="$SCRIPT_DIR/.."

# Versions
ZIG_VERSION="0.15.2"
NINJA_VERSION="v1.13.2"
SDL2_VERSION="2.32.10"
WATCHEXEC_VERSION="2.3.2"

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
    [[ -d "$CORE_DIR/.pio/libdeps" ]] || (cd "$CORE_DIR" && pio pkg install >/dev/null 2>&1)
    return 0
}

setup_wasm_tools() {
    local EMSDK="$TOOLS_DIR/emsdk"
    if ! command -v emcc &>/dev/null; then
        if [[ ! -d "$EMSDK" ]]; then
            log "Installing Emscripten ${DIM}(this may take a while)${NC}"
            git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK"
            "$EMSDK/emsdk.bat" install latest && "$EMSDK/emsdk.bat" activate latest
        fi
        export PATH="$EMSDK/upstream/emscripten:$PATH"
        local NODE_DIR=$(ls -d "$EMSDK/node/"*64bit 2>/dev/null | head -1)
        [[ -d "$NODE_DIR" ]] && export PATH="$NODE_DIR/bin:$PATH"
    fi
    setup_ninja
    [[ -d "$CORE_DIR/.pio/libdeps" ]] || (cd "$CORE_DIR" && pio pkg install >/dev/null 2>&1)
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
    local BUILD_DIR="$SCRIPT_DIR/build/native"
    local start=$(date +%s)
    echo -e "\n${BOLD}${BLUE}═══ Native Build ═══${NC}\n"
    
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        setup_native_tools
        mkdir -p "$BUILD_DIR"
        if ! run_with_spinner "Configuring" cmake -G Ninja -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/zig-toolchain.cmake" \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DPLATFORM_ID=native -DSDL2_ROOT="$TOOLS_DIR/SDL2"; then
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
    success "bin/native/midi_studio_sdl.exe ${DIM}(${elapsed}s)${NC}"
}

do_build_wasm() {
    local BUILD_DIR="$SCRIPT_DIR/build/wasm"
    local EMSDK="$TOOLS_DIR/emsdk"
    local start=$(date +%s)
    echo -e "\n${BOLD}${BLUE}═══ WASM Build ═══${NC}\n"
    
    setup_wasm_tools
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        mkdir -p "$BUILD_DIR" && rm -rf "$BUILD_DIR"/*
        cd "$BUILD_DIR"
        if ! run_with_spinner "Configuring" python "$EMSDK/upstream/emscripten/emcmake.py" cmake "$SCRIPT_DIR/wasm" -G Ninja -DCMAKE_BUILD_TYPE=Release; then
            echo "$BUILD_OUTPUT"
            fail "CMake configuration failed"
        fi
    fi
    
    cd "$SCRIPT_DIR/build/wasm"
    if ! run_ninja_with_progress "$SCRIPT_DIR/build/wasm"; then
        echo "$BUILD_OUTPUT" | grep -E "(error|Error)" | head -10
        fail "Build failed"
    fi
    
    local elapsed=$(($(date +%s) - start))
    local size=$(du -h "$SCRIPT_DIR/bin/wasm/midi_studio_wasm.wasm" 2>/dev/null | cut -f1)
    success "bin/wasm/midi_studio_wasm.html ${DIM}($size, ${elapsed}s)${NC}"
}

# ═══════════════════════════════════════════════════════════════════
# Run / Serve
# ═══════════════════════════════════════════════════════════════════
do_run() {
    do_build_native
    echo -e "\n${CYAN}●${NC} Running...\n"
    "$SCRIPT_DIR/bin/native/midi_studio_sdl.exe"
}

do_serve() {
    do_build_wasm
    echo -e "\n${CYAN}●${NC} Serving at ${BOLD}http://localhost:8000/midi_studio_wasm.html${NC}\n"
    python -m http.server 8000 -d "$SCRIPT_DIR/bin/wasm"
}

# ═══════════════════════════════════════════════════════════════════
# Watch
# ═══════════════════════════════════════════════════════════════════
do_watch_native() {
    setup_watchexec
    do_build_native
    echo -e "\n${CYAN}●${NC} Watching... ${DIM}(Ctrl+C to stop)${NC}"
    echo -e "${DIM}   File change → rebuild → restart${NC}\n"
    
    # Convert to Windows paths for PowerShell
    local build_dir_win=$(cygpath -w "$SCRIPT_DIR/build/native")
    local exe_win=$(cygpath -w "$SCRIPT_DIR/bin/native/midi_studio_sdl.exe")
    
    "$TOOLS_DIR/watchexec/watchexec.exe" \
        -w "$CORE_DIR/src" -w "$SCRIPT_DIR" \
        -e cpp,hpp,h,c \
        --restart \
        --shell=powershell \
        -- "ninja -C '$build_dir_win'; if (\$LASTEXITCODE -eq 0) { & '$exe_win' }"
}

do_watch_wasm() {
    local EMSDK="$TOOLS_DIR/emsdk"
    do_build_wasm
    echo -e "\n${CYAN}●${NC} Serving at ${BOLD}http://localhost:8000/midi_studio_wasm.html${NC}"
    echo -e "${DIM}   emrun serves the WASM app${NC}\n"
    python "$EMSDK/upstream/emscripten/emrun.py" --port 8000 --no_browser "$SCRIPT_DIR/bin/wasm/midi_studio_wasm.html"
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
            rm -rf "$SCRIPT_DIR/build" "$SCRIPT_DIR/bin" 2>/dev/null
            success "Cleaned build/ and bin/"
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

ARG1="${1:-}"
ARG2="${2:-}"

if [[ -z "$ARG1" ]]; then
    interactive_menu
elif is_op "$ARG1"; then
    OP="$ARG1"
    if [[ "$OP" =~ ^(build|watch)$ ]]; then
        if [[ -n "$ARG2" ]]; then
            TARGET="$ARG2"
        else
            ask_target
        fi
    fi
    run_op "$OP" "$TARGET"
elif is_target "$ARG1"; then
    TARGET="$ARG1"
    if is_op "$ARG2"; then
        OP="$ARG2"
    else
        ask_op
    fi
    run_op "$OP" "$TARGET"
else
    echo -e "${BOLD}Usage:${NC} $0 [operation] [target]"
    echo -e "       $0 [target] [operation]"
    echo ""
    echo -e "${DIM}Operations:${NC} build, run, serve, watch, clean"
    echo -e "${DIM}Targets:${NC}    native, wasm"
fi
