#!/bin/bash
# Restart clangd by regenerating compile_commands.json

source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

PROJECT_ROOT="$(find_root)"

# --- Main ---
echo -e "\n${BLUE}═══ Restart clangd ═══${NC}"
echo -e "${DIM}Root: $PROJECT_ROOT${NC}"

cd "$PROJECT_ROOT"

require_cmd pio

log "Cleaning build artifacts..."
pio run -t clean -e debug 2>/dev/null || true

log "Rebuilding to generate compile_commands.json..."
pio run -e debug

echo ""
success "Done! Now restart clangd in VS Code:"
echo -e "${DIM}  Ctrl+Shift+P → 'clangd: Restart language server'${NC}"
