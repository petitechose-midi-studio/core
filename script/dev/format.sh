#!/bin/bash
# Format all C/C++ source files in src/ using clang-format

source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"

PROJECT_ROOT="$(find_root)"

# --- Main ---
echo -e "\n${BLUE}═══ Code Formatter ═══${NC}"
echo -e "${DIM}Root: $PROJECT_ROOT${NC}"

cd "$PROJECT_ROOT"

require_cmd clang-format

log "Formatting C/C++ files in src/..."

count=0
while IFS= read -r -d '' file; do
    clang-format -i "$file"
    ((++count))
done < <(find src -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" \) -print0)

success "Formatted $count files"
