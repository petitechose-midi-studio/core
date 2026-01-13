#!/bin/bash
# Build and run desktop simulator
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Detect platform
case "$OSTYPE" in
    linux*)   PLATFORM="linux"; EXE_EXT="" ;;
    darwin*)  PLATFORM="macos"; EXE_EXT="" ;;
    msys*|mingw*|cygwin*) PLATFORM="windows"; EXE_EXT=".exe" ;;
esac

ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64) ARCH="x86_64" ;;
    aarch64|arm64) ARCH="aarch64" ;;
esac

PLATFORM_ID="${PLATFORM}-${ARCH}"

# Build first
./build.sh "${1:-Debug}"

# Run
echo ""
echo -e "\033[1;36m▶ Running (${PLATFORM_ID})...\033[0m"
echo ""

./bin/${PLATFORM_ID}/midi_studio_desktop${EXE_EXT}
