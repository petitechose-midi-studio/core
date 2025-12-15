# pyright: reportUndefinedVariable=false
"""
Shared utility for USB MIDI SysEx buffer patching.

Used by both pre_build.py (standalone) and lib_pre_build.py (library context).
"""
import os
import re
import glob


def find_config(env):
    """Find App.hpp in core (direct build, sibling, or libdeps)."""
    root = env.subst("$PROJECT_DIR")
    candidates = [
        os.path.join(root, "src/config/App.hpp"),
        os.path.join(root, "../core/src/config/App.hpp"),
        os.path.join(root, "../../src/config/App.hpp"),
        *glob.glob(os.path.join(
            root, ".pio/libdeps/*/petitechose-midi-studio-core/src/config/App.hpp"
        )),
    ]
    return next((p for p in candidates if os.path.exists(p)), None)


def read_sysex_max(env):
    """Read USB_SYSEX_MAX_SIZE from config."""
    path = find_config(env)
    if not path:
        return None
    with open(path, encoding='utf-8') as f:
        match = re.search(r'USB_SYSEX_MAX_SIZE\s*=\s*(\d+)', f.read())
    return int(match.group(1)) if match else None


def patch_usb_midi_sysex(env, exit_func):
    """Patch usb_midi.h with configured SysEx buffer size.

    Args:
        env: PlatformIO environment
        exit_func: Function to call on error (typically Exit from SCons)
    """
    size = read_sysex_max(env)
    if not size:
        print("[ERROR] USB_SYSEX_MAX_SIZE not found")
        exit_func(1)

    framework = env.PioPlatform().get_package_dir("framework-arduinoteensy")
    header = os.path.join(framework, "cores/teensy4/usb_midi.h")

    if not os.path.exists(header):
        return

    with open(header, encoding='utf-8') as f:
        content = f.read()

    match = re.search(r'#define\s+USB_MIDI_SYSEX_MAX\s+(\d+)', content)
    if not match:
        return

    current = int(match.group(1))
    if current == size:
        print(f"[OK] USB_MIDI_SYSEX_MAX = {size}")
        return

    print(f"[PATCH] USB_MIDI_SYSEX_MAX: {current} -> {size}")
    patched = re.sub(r'(#define\s+USB_MIDI_SYSEX_MAX\s+)\d+', rf'\g<1>{size}', content)
    with open(header, 'w', encoding='utf-8') as f:
        f.write(patched)

    # Force rebuild of Arduino framework
    lib = os.path.join(env.subst("$BUILD_DIR"), "libFrameworkArduino.a")
    if os.path.exists(lib):
        os.remove(lib)
