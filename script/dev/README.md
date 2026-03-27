# Development Scripts

Utility scripts for development workflow.

## Scripts

| Script | Purpose |
|--------|---------|
| `format.sh` | Format all C/C++ files in `src/` with clang-format |
| `restart-clangd.sh` | Regenerate `compile_commands.json` and prompt to restart clangd |
| `check-downstream-compat.ps1` | Build a downstream repo such as `plugin-bitwig` against the current `ms-core` export surface |

## Usage

From anywhere in the project:

```bash
# Format entire codebase
./script/dev/format.sh

# Restart clangd (after changing includes, build flags, etc.)
./script/dev/restart-clangd.sh
```

```powershell
# Check that plugin-bitwig still builds against the current core checkout
pwsh ./script/dev/check-downstream-compat.ps1

# Check another downstream project or another PlatformIO environment
pwsh ./script/dev/check-downstream-compat.ps1 -DownstreamProjectPath ..\plugin-bitwig -Environment release
```

## Requirements

- **clang-format**: Must be in PATH (installed with LLVM or clangd extension)
- **PlatformIO CLI**: For `restart-clangd.sh`
- **PlatformIO CLI**: For `check-downstream-compat.ps1`

## Shared Library

These scripts use `script/lib/common.sh` for shared utilities (colors, logging, `find_root()`).
