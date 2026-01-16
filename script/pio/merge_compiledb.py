#!/usr/bin/env python3
"""
Merge compile_commands.json from Teensy (PlatformIO) and SDL (CMake) builds.

This creates a unified compilation database for clangd that:
- Uses SDL compile commands for files in sdl/
- Uses Teensy compile commands for everything else (src/, main.cpp, etc.)

Can be run standalone or called from build scripts.
"""

import json
import os
import sys
from pathlib import Path


def merge_compile_commands(project_root: Path) -> bool:
    """Merge Teensy and SDL compile_commands.json files.

    Strategy:
    - Teensy's compile_commands.json is backed up to compile_commands_teensy.json
    - SDL's compile_commands.json stays in sdl/build/native/
    - Merged result is written to compile_commands.json (used by clangd)

    Priority:
    - Files in sdl/ use SDL compile commands (MinGW)
    - Files elsewhere use Teensy compile commands (ARM)

    Args:
        project_root: Path to the core/ directory

    Returns:
        True if merge was successful, False otherwise
    """
    teensy_cdb = project_root / "compile_commands.json"
    teensy_backup = project_root / "compile_commands_teensy.json"
    sdl_cdb = project_root / "sdl" / "build" / "native" / "compile_commands.json"
    output_cdb = project_root / "compile_commands.json"

    merged = []
    seen_files = set()
    sdl_entries_map = {}  # normalized_path -> entry
    teensy_entries_map = {}  # normalized_path -> entry

    def normalize_path(file_path: str, directory: str = "") -> str:
        """Normalize path to absolute, lowercase, forward-slash format."""
        if not os.path.isabs(file_path):
            # Make relative path absolute using directory from entry
            file_path = os.path.join(directory, file_path)
        return os.path.normpath(file_path).replace("\\", "/").lower()

    # If Teensy file exists and no backup, create backup
    # (This preserves the original Teensy-only file)
    if teensy_cdb.exists() and not teensy_backup.exists():
        import shutil

        shutil.copy(teensy_cdb, teensy_backup)

    # Use backup if it exists (it's the "pure" Teensy file)
    teensy_source = teensy_backup if teensy_backup.exists() else teensy_cdb

    # Load SDL compile commands into map
    if sdl_cdb.exists():
        with open(sdl_cdb, "r", encoding="utf-8") as f:
            for entry in json.load(f):
                file_path = entry.get("file", "")
                directory = entry.get("directory", "")
                norm_path = normalize_path(file_path, directory)
                sdl_entries_map[norm_path] = entry

    # Load Teensy compile commands into map
    if teensy_source.exists():
        with open(teensy_source, "r", encoding="utf-8") as f:
            for entry in json.load(f):
                file_path = entry.get("file", "")
                directory = entry.get("directory", str(project_root))
                norm_path = normalize_path(file_path, directory)
                teensy_entries_map[norm_path] = entry

    # Merge with correct priority:
    # - Files in sdl/ -> use SDL compile commands
    # - Files elsewhere -> use Teensy compile commands (if available), else SDL
    all_files = set(sdl_entries_map.keys()) | set(teensy_entries_map.keys())

    for norm_path in all_files:
        is_sdl_file = "/sdl/" in norm_path

        if is_sdl_file:
            # SDL-specific files: use SDL compile commands only
            if norm_path in sdl_entries_map:
                merged.append(sdl_entries_map[norm_path])
                seen_files.add(norm_path)
        else:
            # Non-SDL files: prefer Teensy, fallback to SDL
            if norm_path in teensy_entries_map:
                merged.append(teensy_entries_map[norm_path])
                seen_files.add(norm_path)
            elif norm_path in sdl_entries_map:
                # Fallback: use SDL if no Teensy entry (shouldn't happen normally)
                merged.append(sdl_entries_map[norm_path])
                seen_files.add(norm_path)

    # Load Teensy compile commands (skip files already covered by SDL)
    if teensy_source.exists():
        with open(teensy_source, "r", encoding="utf-8") as f:
            teensy_entries = json.load(f)

        for entry in teensy_entries:
            file_path = entry.get("file", "")
            norm_path = os.path.normpath(file_path).replace("\\", "/").lower()

            # Skip if already in SDL (SDL takes precedence for its files)
            if norm_path in seen_files:
                continue

            # Skip SDL-specific files that shouldn't use Teensy config
            if "/sdl/" in norm_path or "\\sdl\\" in norm_path.replace("/", "\\"):
                continue

            merged.append(entry)
            seen_files.add(norm_path)

    if not merged:
        print("Warning: No compile commands found to merge")
        return False

    # Write merged file
    with open(output_cdb, "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2)

    sdl_count = len(
        [e for e in merged if "/sdl/" in e.get("file", "").replace("\\", "/")]
    )
    teensy_count = len(merged) - sdl_count
    print(
        f"Merged compile_commands.json: {teensy_count} Teensy + {sdl_count} SDL entries"
    )
    return True


def main():
    """CLI entry point."""
    # Determine project root
    if len(sys.argv) > 1:
        project_root = Path(sys.argv[1])
    else:
        # Default: assume we're in script/pio/
        script_dir = Path(__file__).parent
        project_root = script_dir.parent.parent

    if not project_root.exists():
        print(f"Error: Project root not found: {project_root}")
        sys.exit(1)

    success = merge_compile_commands(project_root)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
