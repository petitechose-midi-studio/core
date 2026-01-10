#!/bin/bash
# =============================================================================
# Auto-download build tools (Zig, CMake, Ninja) - Always latest versions
# Standalone script - downloads everything into desktop/tools/
# =============================================================================
set -e

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; GRAY='\033[0;90m'; BOLD='\033[1m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR/tools"

echo ""
echo -e "${BOLD}Initializing Build Tools${NC}"
echo -e "${GRAY}─────────────────────────────────────────${NC}"

# =============================================================================
# Platform detection
# =============================================================================
detect_platform() {
    case "$OSTYPE" in
        linux*)   PLATFORM="linux"; EXE_EXT="" ;;
        darwin*)  PLATFORM="macos"; EXE_EXT="" ;;
        msys*|mingw*|cygwin*) PLATFORM="windows"; EXE_EXT=".exe" ;;
        *)
            echo -e "${RED}Unsupported platform: $OSTYPE${NC}"
            exit 1
            ;;
    esac

    ARCH=$(uname -m)
    case "$ARCH" in
        x86_64|amd64) ARCH="x86_64" ;;
        aarch64|arm64) ARCH="aarch64" ;;
        *)
            echo -e "${RED}Unsupported architecture: $ARCH${NC}"
            exit 1
            ;;
    esac

    echo -e "  ${CYAN}→${NC} Platform: ${CYAN}${PLATFORM}-${ARCH}${NC}"
}

# =============================================================================
# Download helper
# =============================================================================
download() {
    local url="$1"
    local output="$2"

    if command -v curl &>/dev/null; then
        curl -fsSL "$url" -o "$output"
    elif command -v wget &>/dev/null; then
        wget -q "$url" -O "$output"
    else
        echo -e "${RED}Neither curl nor wget found${NC}"
        exit 1
    fi
}

# =============================================================================
# Get latest GitHub release tag
# =============================================================================
get_latest_release() {
    local repo="$1"
    local api_url="https://api.github.com/repos/${repo}/releases/latest"
    local result

    if command -v curl &>/dev/null; then
        result=$(curl -fsSL "$api_url" 2>/dev/null)
    else
        result=$(wget -qO- "$api_url" 2>/dev/null)
    fi

    echo "$result" | grep '"tag_name"' | head -1 | sed -E 's/.*"([^"]+)".*/\1/'
}

# =============================================================================
# Zig - Latest from ziglang.org
# =============================================================================
install_zig() {
    echo -e "  ${CYAN}→${NC} Checking Zig..."

    # Get latest version from ziglang.org index
    local index_url="https://ziglang.org/download/index.json"
    local zig_info
    zig_info=$(curl -fsSL "$index_url" 2>/dev/null)

    # Extract latest stable version (not master)
    local latest_version
    latest_version=$(echo "$zig_info" | grep -oE '"[0-9]+\.[0-9]+\.[0-9]+"' | head -1 | tr -d '"')

    if [[ -z "$latest_version" ]]; then
        echo -e "${RED}    Could not detect latest Zig version${NC}"
        exit 1
    fi

    # Platform mapping for Zig
    local zig_platform zig_ext
    case "${PLATFORM}-${ARCH}" in
        linux-x86_64)   zig_platform="x86_64-linux"; zig_ext="tar.xz" ;;
        linux-aarch64)  zig_platform="aarch64-linux"; zig_ext="tar.xz" ;;
        macos-x86_64)   zig_platform="x86_64-macos"; zig_ext="tar.xz" ;;
        macos-aarch64)  zig_platform="aarch64-macos"; zig_ext="tar.xz" ;;
        windows-x86_64) zig_platform="x86_64-windows"; zig_ext="zip" ;;
        windows-aarch64) zig_platform="aarch64-windows"; zig_ext="zip" ;;
        *)
            echo -e "${RED}    Unsupported platform for Zig: ${PLATFORM}-${ARCH}${NC}"
            exit 1
            ;;
    esac

    local zig_dir="$TOOLS_DIR/zig"
    local version_file="$zig_dir/.version"

    # Check if already installed with correct version
    if [[ -f "$version_file" ]] && [[ "$(cat "$version_file")" == "$latest_version" ]]; then
        echo -e "    ${GREEN}✓${NC} Zig ${latest_version} ${GRAY}(cached)${NC}"
        ZIG_DIR="$zig_dir"
        return
    fi

    echo -e "    Downloading Zig ${CYAN}${latest_version}${NC}..."

    local download_url="https://ziglang.org/download/${latest_version}/zig-${zig_platform}-${latest_version}.${zig_ext}"
    local tmp_file="$TOOLS_DIR/zig_download"

    rm -rf "$zig_dir" "$tmp_file"*
    mkdir -p "$TOOLS_DIR"

    if [[ "$zig_ext" == "zip" ]]; then
        download "$download_url" "$tmp_file.zip"
        unzip -q "$tmp_file.zip" -d "$TOOLS_DIR"
        rm "$tmp_file.zip"
    else
        download "$download_url" "$tmp_file.tar.xz"
        tar -xf "$tmp_file.tar.xz" -C "$TOOLS_DIR"
        rm "$tmp_file.tar.xz"
    fi

    # Find extracted directory and rename to simple "zig"
    local extracted=$(ls -d "$TOOLS_DIR"/zig-* 2>/dev/null | head -1)
    mv "$extracted" "$zig_dir"

    echo "$latest_version" > "$version_file"
    echo -e "    ${GREEN}✓${NC} Zig ${latest_version} installed"
    ZIG_DIR="$zig_dir"
}

# =============================================================================
# CMake - Latest from GitHub
# =============================================================================
install_cmake() {
    echo -e "  ${CYAN}→${NC} Checking CMake..."

    local latest_tag
    latest_tag=$(get_latest_release "Kitware/CMake")
    local latest_version="${latest_tag#v}"  # Remove 'v' prefix

    if [[ -z "$latest_version" ]]; then
        echo -e "${RED}    Could not detect latest CMake version${NC}"
        exit 1
    fi

    local cmake_dir="$TOOLS_DIR/cmake"
    local version_file="$cmake_dir/.version"

    if [[ -f "$version_file" ]] && [[ "$(cat "$version_file")" == "$latest_version" ]]; then
        echo -e "    ${GREEN}✓${NC} CMake ${latest_version} ${GRAY}(cached)${NC}"
        CMAKE_DIR="$cmake_dir"
        return
    fi

    echo -e "    Downloading CMake ${CYAN}${latest_version}${NC}..."

    # Platform mapping
    local cmake_platform cmake_ext
    case "${PLATFORM}-${ARCH}" in
        linux-x86_64)   cmake_platform="linux-x86_64"; cmake_ext="tar.gz" ;;
        linux-aarch64)  cmake_platform="linux-aarch64"; cmake_ext="tar.gz" ;;
        macos-*)        cmake_platform="macos-universal"; cmake_ext="tar.gz" ;;
        windows-x86_64) cmake_platform="windows-x86_64"; cmake_ext="zip" ;;
        windows-aarch64) cmake_platform="windows-arm64"; cmake_ext="zip" ;;
        *)
            echo -e "${RED}    Unsupported platform for CMake${NC}"
            exit 1
            ;;
    esac

    local download_url="https://github.com/Kitware/CMake/releases/download/${latest_tag}/cmake-${latest_version}-${cmake_platform}.${cmake_ext}"
    local tmp_file="$TOOLS_DIR/cmake_download"

    rm -rf "$cmake_dir" "$tmp_file"*
    mkdir -p "$TOOLS_DIR"

    if [[ "$cmake_ext" == "zip" ]]; then
        download "$download_url" "$tmp_file.zip"
        unzip -q "$tmp_file.zip" -d "$TOOLS_DIR"
        rm "$tmp_file.zip"
    else
        download "$download_url" "$tmp_file.tar.gz"
        tar -xzf "$tmp_file.tar.gz" -C "$TOOLS_DIR"
        rm "$tmp_file.tar.gz"
    fi

    local extracted=$(ls -d "$TOOLS_DIR"/cmake-* 2>/dev/null | head -1)
    mv "$extracted" "$cmake_dir"

    echo "$latest_version" > "$version_file"
    echo -e "    ${GREEN}✓${NC} CMake ${latest_version} installed"
    CMAKE_DIR="$cmake_dir"
}

# =============================================================================
# Ninja - Latest from GitHub
# =============================================================================
install_ninja() {
    echo -e "  ${CYAN}→${NC} Checking Ninja..."

    local latest_tag
    latest_tag=$(get_latest_release "ninja-build/ninja")
    local latest_version="${latest_tag#v}"

    if [[ -z "$latest_version" ]]; then
        echo -e "${RED}    Could not detect latest Ninja version${NC}"
        exit 1
    fi

    local ninja_dir="$TOOLS_DIR/ninja"
    local version_file="$ninja_dir/.version"

    if [[ -f "$version_file" ]] && [[ "$(cat "$version_file")" == "$latest_version" ]]; then
        echo -e "    ${GREEN}✓${NC} Ninja ${latest_version} ${GRAY}(cached)${NC}"
        NINJA_DIR="$ninja_dir"
        return
    fi

    echo -e "    Downloading Ninja ${CYAN}${latest_version}${NC}..."

    # Platform mapping (Ninja only has one binary per OS)
    local ninja_platform
    case "${PLATFORM}" in
        linux)   ninja_platform="linux" ;;
        macos)   ninja_platform="mac" ;;
        windows) ninja_platform="win" ;;
    esac

    local download_url="https://github.com/ninja-build/ninja/releases/download/${latest_tag}/ninja-${ninja_platform}.zip"
    local tmp_file="$TOOLS_DIR/ninja_download.zip"

    rm -rf "$ninja_dir" "$tmp_file"
    mkdir -p "$ninja_dir"

    download "$download_url" "$tmp_file"
    unzip -q "$tmp_file" -d "$ninja_dir"
    rm "$tmp_file"

    chmod +x "$ninja_dir/ninja"* 2>/dev/null || true

    echo "$latest_version" > "$version_file"
    echo -e "    ${GREEN}✓${NC} Ninja ${latest_version} installed"
    NINJA_DIR="$ninja_dir"
}

# =============================================================================
# Create Zig wrapper scripts
# =============================================================================
create_wrappers() {
    echo -e "  ${CYAN}→${NC} Creating Zig wrappers..."

    if [[ "$PLATFORM" == "windows" ]]; then
        # Windows batch wrappers
        local zig_path_win
        zig_path_win=$(cygpath -w "$ZIG_DIR/zig.exe" 2>/dev/null || echo "$ZIG_DIR/zig.exe")

        cat > "$TOOLS_DIR/zig-cc.cmd" << WRAPPER_EOF
@echo off
"${zig_path_win}" cc %*
WRAPPER_EOF

        cat > "$TOOLS_DIR/zig-cxx.cmd" << WRAPPER_EOF
@echo off
"${zig_path_win}" c++ %*
WRAPPER_EOF

        cat > "$TOOLS_DIR/zig-ar.cmd" << WRAPPER_EOF
@echo off
"${zig_path_win}" ar %*
WRAPPER_EOF

        ZIG_CC="$TOOLS_DIR/zig-cc.cmd"
        ZIG_CXX="$TOOLS_DIR/zig-cxx.cmd"
        ZIG_AR="$TOOLS_DIR/zig-ar.cmd"
    else
        # Unix shell wrappers
        cat > "$TOOLS_DIR/zig-cc" << WRAPPER_EOF
#!/bin/sh
exec "$ZIG_DIR/zig" cc "\$@"
WRAPPER_EOF
        chmod +x "$TOOLS_DIR/zig-cc"

        cat > "$TOOLS_DIR/zig-cxx" << WRAPPER_EOF
#!/bin/sh
exec "$ZIG_DIR/zig" c++ "\$@"
WRAPPER_EOF
        chmod +x "$TOOLS_DIR/zig-cxx"

        cat > "$TOOLS_DIR/zig-ar" << WRAPPER_EOF
#!/bin/sh
exec "$ZIG_DIR/zig" ar "\$@"
WRAPPER_EOF
        chmod +x "$TOOLS_DIR/zig-ar"

        ZIG_CC="$TOOLS_DIR/zig-cc"
        ZIG_CXX="$TOOLS_DIR/zig-cxx"
        ZIG_AR="$TOOLS_DIR/zig-ar"
    fi

    echo -e "    ${GREEN}✓${NC} Wrappers created"
}

# =============================================================================
# Create toolchain file for Zig
# =============================================================================
create_toolchain() {
    local toolchain_file="$TOOLS_DIR/zig-toolchain.cmake"

    # Get paths for toolchain (CMake format)
    local cmake_cc cmake_cxx cmake_ar
    if [[ "$PLATFORM" == "windows" ]]; then
        cmake_cc=$(cygpath -m "$TOOLS_DIR/zig-cc.cmd" 2>/dev/null || echo "$TOOLS_DIR/zig-cc.cmd")
        cmake_cxx=$(cygpath -m "$TOOLS_DIR/zig-cxx.cmd" 2>/dev/null || echo "$TOOLS_DIR/zig-cxx.cmd")
        cmake_ar=$(cygpath -m "$TOOLS_DIR/zig-ar.cmd" 2>/dev/null || echo "$TOOLS_DIR/zig-ar.cmd")
    else
        cmake_cc="$TOOLS_DIR/zig-cc"
        cmake_cxx="$TOOLS_DIR/zig-cxx"
        cmake_ar="$TOOLS_DIR/zig-ar"
    fi

    cat > "$toolchain_file" << TOOLCHAIN_EOF
# Zig C/C++ Toolchain for CMake (auto-generated)
cmake_minimum_required(VERSION 3.14)

# Wrapper scripts (call zig cc / zig c++)
set(CMAKE_C_COMPILER "${cmake_cc}")
set(CMAKE_CXX_COMPILER "${cmake_cxx}")
set(CMAKE_AR "${cmake_ar}" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "true" CACHE FILEPATH "Ranlib (not needed with Zig)")

# Tell CMake this is Clang-compatible
set(CMAKE_C_COMPILER_ID "Clang")
set(CMAKE_CXX_COMPILER_ID "Clang")
set(CMAKE_C_COMPILER_FRONTEND_VARIANT "GNU")
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "GNU")

# Skip compiler tests (wrappers work)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
TOOLCHAIN_EOF

    echo -e "  ${GREEN}✓${NC} Toolchain file created"
    TOOLCHAIN_FILE="$toolchain_file"
}

# =============================================================================
# Create .env file
# =============================================================================
create_env() {
    cat > "$SCRIPT_DIR/.env" << ENV_EOF
# Build tools configuration (auto-generated by init_tools.sh)
# Re-run init_tools.sh to update to latest versions
ZIG_DIR=$ZIG_DIR
CMAKE_DIR=$CMAKE_DIR
NINJA_DIR=$NINJA_DIR
TOOLCHAIN_FILE=$TOOLCHAIN_FILE
ENV_EOF

    echo -e "  ${GREEN}✓${NC} .env created"
}

# =============================================================================
# Main
# =============================================================================
mkdir -p "$TOOLS_DIR"
detect_platform
install_zig
install_cmake
install_ninja
create_wrappers
create_toolchain
create_env

echo ""
echo -e "${GREEN}All tools ready!${NC}"
echo -e "${GRAY}Run ./build.sh to compile${NC}"
echo ""
