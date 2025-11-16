"""
PlatformIO pre-build script to automatically patch USB_MIDI_SYSEX_MAX
This reads the desired buffer size from src/config/System.hpp and patches the Teensy framework
This ensures the SysEx buffer size is maintained even after framework updates
"""
import os
import re

Import("env")

def read_sysex_size_from_config():
    """Read USB_SYSEX_MAX_SIZE from System.hpp"""
    config_file = os.path.join(env.subst("$PROJECT_DIR"), "src", "config", "System.hpp")

    if not os.path.exists(config_file):
        print(f"[WARNING] System.hpp not found at {config_file}")
        return None

    try:
        with open(config_file, 'r', encoding='utf-8') as f:
            content = f.read()

        # Find: constexpr size_t USB_SYSEX_MAX_SIZE = 2000;
        match = re.search(r'constexpr\s+size_t\s+USB_SYSEX_MAX_SIZE\s+=\s+(\d+)', content)
        if match:
            value = int(match.group(1))
            print(f"[INFO] Read USB_SYSEX_MAX_SIZE from config: {value} bytes")
            return value
        else:
            print("[WARNING] USB_SYSEX_MAX_SIZE not found in System.hpp")
            return None

    except Exception as e:
        print(f"[ERROR] Error reading System.hpp: {e}")
        return None

# Get desired value from config
DESIRED_SYSEX_MAX = read_sysex_size_from_config()
if DESIRED_SYSEX_MAX is None:
    print("[ERROR] Cannot proceed without USB_SYSEX_MAX_SIZE configuration")
    Exit(1)

def patch_usb_midi_header():
    """Patch usb_midi.h to increase USB_MIDI_SYSEX_MAX"""

    # Get framework path
    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-arduinoteensy")

    if not framework_dir:
        print("[WARNING] Teensy framework directory not found")
        return

    usb_midi_h = os.path.join(framework_dir, "cores", "teensy4", "usb_midi.h")

    if not os.path.exists(usb_midi_h):
        print(f"[WARNING] usb_midi.h not found at {usb_midi_h}")
        return

    # Read the file
    try:
        with open(usb_midi_h, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"[ERROR] Error reading usb_midi.h: {e}")
        return

    # Check current value using regex to find the #define line
    match = re.search(r'#define\s+USB_MIDI_SYSEX_MAX\s+(\d+)', content)

    if not match:
        print("[WARNING] Could not find USB_MIDI_SYSEX_MAX definition")
        return

    current_value = int(match.group(1))

    # Check if already patched
    if current_value == DESIRED_SYSEX_MAX:
        print(f"[OK] USB_MIDI_SYSEX_MAX already set to {DESIRED_SYSEX_MAX} bytes")
        return

    # Patch the file
    print(f"[PATCH] Patching USB_MIDI_SYSEX_MAX: {current_value} -> {DESIRED_SYSEX_MAX} bytes")

    # Replace the value, preserving any comments
    new_content = re.sub(
        r'(#define\s+USB_MIDI_SYSEX_MAX\s+)\d+',
        r'\g<1>' + str(DESIRED_SYSEX_MAX),
        content
    )

    # Write back
    try:
        with open(usb_midi_h, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"[SUCCESS] Successfully patched {usb_midi_h}")

        # Force framework rebuild by removing the compiled library
        framework_lib = os.path.join(env.subst("$BUILD_DIR"), "libFrameworkArduino.a")
        if os.path.exists(framework_lib):
            os.remove(framework_lib)
            print("[INFO] Forcing framework rebuild...")

    except Exception as e:
        print(f"[ERROR] Error writing usb_midi.h: {e}")
        return

# Run the patch function
patch_usb_midi_header()
