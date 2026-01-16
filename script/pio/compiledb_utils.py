# pyright: reportUndefinedVariable=false
"""
Shared utilities for compile_commands.json generation.

Used by both core (standalone) and plugins to ensure consistent clangd support.
Includes merging of Teensy and SDL compile databases for unified clangd support.
"""

import json
import os
from pathlib import Path


def patch_compiler_paths(cdb_path, env):
    """Post-process compile_commands.json to use full compiler paths.

    This is needed because SCons may generate relative compiler names
    which clangd cannot resolve without the toolchain in PATH.
    """
    if not os.path.exists(cdb_path):
        return

    # Get toolchain directory
    toolchain_dir = env.PioPlatform().get_package_dir("toolchain-gccarmnoneeabi-teensy")
    bin_dir = os.path.join(toolchain_dir, "bin")

    # Build full paths for compilers
    cxx_path = os.path.join(bin_dir, "arm-none-eabi-g++.exe")
    cc_path = os.path.join(bin_dir, "arm-none-eabi-gcc.exe")

    with open(cdb_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    for entry in data:
        cmd = entry.get("command", "")
        # Replace bare compiler names with full paths (only at start of command)
        if cmd.startswith("arm-none-eabi-g++ "):
            cmd = cxx_path + cmd[len("arm-none-eabi-g++") :]
        elif cmd.startswith("arm-none-eabi-gcc "):
            cmd = cc_path + cmd[len("arm-none-eabi-gcc") :]
        entry["command"] = cmd

    with open(cdb_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)


def merge_with_sdl(project_dir):
    """Merge Teensy compile_commands.json with SDL's if it exists.

    Creates compile_commands_merged.json at project root.
    """
    from merge_compiledb import merge_compile_commands

    merge_compile_commands(Path(project_dir))


def setup_compile_commands(env):
    """Generate compile_commands.json for clangd IDE integration.

    Sets up SCons to generate the compilation database and adds a post-build
    action to patch compiler paths for clangd compatibility, then merges with
    SDL compile database if available.
    """

    def _patch_and_merge_action(source, target, env):
        patch_compiler_paths(str(target[0]), env)
        # Merge with SDL compile_commands.json if it exists
        project_dir = env.subst("$PROJECT_DIR")
        try:
            merge_with_sdl(project_dir)
        except Exception as e:
            print(f"Note: Could not merge with SDL compile_commands: {e}")

    env.Tool("compilation_db")
    cdb_path = os.path.join(env.subst("$PROJECT_DIR"), "compile_commands.json")
    cdb = env.CompilationDatabase(cdb_path)
    env.AlwaysBuild(cdb)
    env.Default(cdb)
    env.AddPostAction(cdb, _patch_and_merge_action)
