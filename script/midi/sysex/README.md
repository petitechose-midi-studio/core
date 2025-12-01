# USB MIDI SysEx Patcher

PlatformIO pre-build script that patches the Teensy framework's USB MIDI SysEx buffer size.

## Purpose

The Teensy USB MIDI library has a default SysEx buffer size (`USB_MIDI_SYSEX_MAX`) that may be too small for your application. This script automatically patches the framework header to match your configured value.

## How It Works

1. Reads `USB_SYSEX_MAX_SIZE` from `src/config/System.hpp`
2. Patches `cores/teensy4/usb_midi.h` in the Arduino framework
3. Forces rebuild of the Arduino framework if changed

## Configuration

In `src/config/System.hpp`:

```cpp
namespace System::Midi {
    constexpr uint16_t USB_SYSEX_MAX_SIZE = 512;  // Your desired buffer size
}
```

## PlatformIO Integration

Add to `platformio.ini`:

```ini
extra_scripts =
    pre:script/midi/sysex/patch_usb_midi_sysex.py
```

The script runs automatically before each build.

## Output

```
[OK] USB_MIDI_SYSEX_MAX = 512        # Already patched
[PATCH] USB_MIDI_SYSEX_MAX: 256 → 512  # Patching now
[ERROR] USB_SYSEX_MAX_SIZE not found   # Config not found
```

## File Search Paths

The script searches for `System.hpp` in:
1. `src/config/System.hpp` (direct build)
2. `../core/src/config/System.hpp` (sibling project)
3. `../../src/config/System.hpp` (nested project)
4. `.pio/libdeps/*/petitechose-midi-studio-core/src/config/System.hpp` (library dependency)

## Why This Is Needed

The Teensy USB MIDI implementation has a compile-time buffer size that cannot be changed at runtime. Since the framework is installed globally by PlatformIO, this script patches it in-place to support larger SysEx messages (e.g., for preset dumps, firmware updates).
