# pyright: reportUndefinedVariable=false, reportUnknownArgumentType=false, reportUnknownVariableType=false, reportUnknownMemberType=false
"""PlatformIO pre-build: patch USB_MIDI_SYSEX_MAX from System.hpp config."""
import os, re, glob  # noqa: E401

Import("env")

def find_config():
    """Find System.hpp in core (direct build, sibling, or libdeps)."""
    root = env.subst("$PROJECT_DIR")
    candidates = [
        os.path.join(root, "src/config/System.hpp"),
        os.path.join(root, "../core/src/config/System.hpp"),
        os.path.join(root, "../../src/config/System.hpp"),
        *glob.glob(os.path.join(root, ".pio/libdeps/*/petitechose-midi-studio-core/src/config/System.hpp")),
    ]
    return next((p for p in candidates if os.path.exists(p)), None)

def read_sysex_max():
    """Read USB_SYSEX_MAX_SIZE from config."""
    path = find_config()
    if not path:
        return None
    match = re.search(r'USB_SYSEX_MAX_SIZE\s*=\s*(\d+)', open(path).read())
    return int(match.group(1)) if match else None

def patch():
    """Patch usb_midi.h with configured SysEx buffer size."""
    size = read_sysex_max()
    if not size:
        print("[ERROR] USB_SYSEX_MAX_SIZE not found")
        Exit(1)

    framework = env.PioPlatform().get_package_dir("framework-arduinoteensy")
    header = os.path.join(framework, "cores/teensy4/usb_midi.h")

    if not os.path.exists(header):
        return

    content = open(header).read()
    match = re.search(r'#define\s+USB_MIDI_SYSEX_MAX\s+(\d+)', content)
    if not match:
        return

    current = int(match.group(1))
    if current == size:
        print(f"[OK] USB_MIDI_SYSEX_MAX = {size}")
        return

    print(f"[PATCH] USB_MIDI_SYSEX_MAX: {current} → {size}")
    open(header, 'w').write(re.sub(r'(#define\s+USB_MIDI_SYSEX_MAX\s+)\d+', rf'\g<1>{size}', content))

    # Force rebuild
    lib = os.path.join(env.subst("$BUILD_DIR"), "libFrameworkArduino.a")
    if os.path.exists(lib):
        os.remove(lib)

patch()
