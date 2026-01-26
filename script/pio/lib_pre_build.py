# pyright: reportUndefinedVariable=false, reportMissingImports=false
"""
PlatformIO pre-build script for MIDI Studio Core.
Patches USB MIDI SysEx buffer size in the Teensy framework.
"""
import os
import re
import json

Import("env")


def find_core_path(project_dir):
    """Find core library path from .pio-link files or direct folders."""
    libdeps = os.path.join(project_dir, ".pio/libdeps")

    if not os.path.isdir(libdeps):
        return None

    for env_name in os.listdir(libdeps):
        env_path = os.path.join(libdeps, env_name)
        if not os.path.isdir(env_path):
            continue

        # Check .pio-link file (Windows symlink)
        link_file = os.path.join(env_path, "ms-core.pio-link")
        if os.path.exists(link_file):
            with open(link_file, encoding='utf-8') as f:
                data = json.load(f)
            uri = data.get("spec", {}).get("uri", "")
            if uri.startswith("symlink://"):
                rel_path = uri.replace("symlink://", "")
                return os.path.normpath(os.path.join(project_dir, rel_path))

        # Check direct folder (GitHub release)
        direct = os.path.join(env_path, "ms-core")
        if os.path.isdir(direct):
            return direct

    return None


project_dir = env.subst("$PROJECT_DIR")

# 1. Find App.hpp
config_path = os.path.join(project_dir, "src/config/App.hpp")

if not os.path.exists(config_path):
    core_path = find_core_path(project_dir)
    if core_path:
        config_path = os.path.join(core_path, "src/config/App.hpp")

if not os.path.exists(config_path):
    print("[ERROR] App.hpp not found")
    Exit(1)
    raise SystemExit  # For type checker

# 2. Read USB_SYSEX_MAX_SIZE
with open(config_path, encoding='utf-8') as f:
    match = re.search(r'USB_SYSEX_MAX_SIZE\s*=\s*(\d+)', f.read())

if not match:
    print("[ERROR] USB_SYSEX_MAX_SIZE not found in App.hpp")
    Exit(1)
    raise SystemExit  # For type checker

size = int(match.group(1))

# 3. Patch usb_midi.h if needed
usb_midi_h = os.path.join(
    env.PioPlatform().get_package_dir("framework-arduinoteensy"),
    "cores/teensy4/usb_midi.h"
)

if os.path.exists(usb_midi_h):
    with open(usb_midi_h, encoding='utf-8') as f:
        content = f.read()

    match = re.search(r'#define\s+USB_MIDI_SYSEX_MAX\s+(\d+)', content)
    if match:
        current = int(match.group(1))
        if current == size:
            print(f"[OK] USB_MIDI_SYSEX_MAX = {size}")
        else:
            print(f"[PATCH] USB_MIDI_SYSEX_MAX: {current} -> {size}")
            patched = re.sub(r'(#define\s+USB_MIDI_SYSEX_MAX\s+)\d+', rf'\g<1>{size}', content)
            with open(usb_midi_h, 'w', encoding='utf-8') as f:
                f.write(patched)
            # Force rebuild of Arduino framework
            lib_path = os.path.join(env.subst("$BUILD_DIR"), "libFrameworkArduino.a")
            if os.path.exists(lib_path):
                os.remove(lib_path)
