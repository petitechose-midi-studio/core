#!/usr/bin/env bash
# Generate LVGL icon fonts (core)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

if [[ -n "${WORKSPACE_ROOT:-}" && -f "$WORKSPACE_ROOT/open-control/ui-lvgl-cli-tools/icon/build.py" ]]; then
  WORKSPACE="$WORKSPACE_ROOT"
else
  WORKSPACE="$(cd "$CORE_ROOT/../.." && pwd)"
fi

BUILDER="$WORKSPACE/open-control/ui-lvgl-cli-tools/icon/build.py"
[[ -f "$BUILDER" ]] || { echo "Builder not found: $BUILDER" >&2; exit 1; }

PY="$WORKSPACE/.venv/bin/python"
if [[ -f "$WORKSPACE/.venv/Scripts/python.exe" ]]; then
  PY="$WORKSPACE/.venv/Scripts/python.exe"
fi

if [[ ! -x "$PY" ]]; then
  if command -v python3 >/dev/null 2>&1; then
    PY=python3
  else
    PY=python
  fi
fi

cd "$CORE_ROOT"
exec "$PY" "$BUILDER" "$@"
