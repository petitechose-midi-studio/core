#!/bin/bash
# Build and run desktop simulator
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Build first
./build.sh "${1:-Debug}"

# Run
echo ""
echo -e "\033[1;36m▶ Running...\033[0m"
echo ""

case "$OSTYPE" in
    msys*|mingw*|cygwin*) ./bin/midi_studio_desktop.exe ;;
    *) ./bin/midi_studio_desktop ;;
esac
