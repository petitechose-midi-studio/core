# pyright: reportUndefinedVariable=false, reportMissingImports=false
"""
PlatformIO pre-build script for MIDI Studio Core (library context).

This script is used when core is included as a library dependency.
It handles USB MIDI SysEx buffer patching.

Note: compile_commands.json generation must be done via the plugin's own
extra_scripts because compilation_db doesn't work in library context.
"""
import os
import sys
import glob

Import("env")

# Find script directory (in library context, we need to locate it via libdeps or symlink)
project_dir = env.subst("$PROJECT_DIR")
candidates = [
    os.path.join(project_dir, "../core/script/build"),  # Symlink: ../core
    *glob.glob(os.path.join(
        project_dir, ".pio/libdeps/*/petitechose-midi-studio-core/script/build"
    )),
]
script_dir = next((p for p in candidates if os.path.exists(p)), None)

if script_dir:
    sys.path.insert(0, script_dir)
    from sysex_patch import patch_usb_midi_sysex
    patch_usb_midi_sysex(env, Exit)
else:
    print("[WARNING] Could not find core script directory")
