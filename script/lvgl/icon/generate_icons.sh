#!/bin/bash
# Icon Font Builder - Wrapper for unified builder
# Usage: ./generate_icons.sh

set -e

# Get script directory and derive paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# core/script/lvgl/icon -> core -> midi-studio
CORE_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"
MIDI_STUDIO_ROOT="$(dirname "$CORE_ROOT")"
BUILDER="$MIDI_STUDIO_ROOT/script/icon/builder.py"

# Check builder exists
[[ -f "$BUILDER" ]] || { echo "Builder not found: $BUILDER" >&2; exit 1; }

# Run unified builder for core
python "$BUILDER" core "$@"
