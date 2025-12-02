# Development Scripts

Utility scripts for development workflow.

## Scripts

| Script | Purpose |
|--------|---------|
| `format.sh` | Format all C/C++ files in `src/` with clang-format |
| `restart-clangd.sh` | Regenerate `compile_commands.json` and prompt to restart clangd |

## Usage

From anywhere in the project:

```bash
# Format entire codebase
./script/dev/format.sh

# Restart clangd (after changing includes, build flags, etc.)
./script/dev/restart-clangd.sh
```

## Requirements

- **clang-format**: Must be in PATH (installed with LLVM or clangd extension)
- **PlatformIO CLI**: For `restart-clangd.sh`

## Shared Library

These scripts use `script/lib/common.sh` for shared utilities (colors, logging, `find_root()`).
