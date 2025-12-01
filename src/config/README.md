# Config

Compile-time configuration for MIDI Studio Core.

## Overview

All configuration values are `constexpr` for zero runtime overhead. This directory contains:

- System-wide constants (pins, timing, display)
- Hardware input definitions (buttons, encoders)
- Input identifiers (ButtonID, EncoderID)
- Version information

## Files

| File | Description |
|------|-------------|
| `System.hpp` | System-wide constants |
| `InputDefinition.hpp` | Hardware button/encoder definitions |
| `InputID.hpp` | ButtonID and EncoderID enums |
| `MidiMapping.hpp` | Default MIDI CC mappings |
| `Version.hpp` | Core and API version numbers |
| `ui/lv_conf.h` | LVGL configuration |

---

## System.hpp

System-wide constants organized by namespace.

### System::Application

```cpp
constexpr const char* NAME = "Midi Studio";
// Version imported from Version.hpp
```

### System::Hardware

Pin assignments for hardware peripherals.

```cpp
// Display pins (ILI9341 SPI)
constexpr uint8_t DISPLAY_CS_PIN = 28;
constexpr uint8_t DISPLAY_DC_PIN = 0;
constexpr uint8_t DISPLAY_RST_PIN = 29;
constexpr uint8_t DISPLAY_MOSI_PIN = 26;
constexpr uint8_t DISPLAY_SCK_PIN = 27;
constexpr uint8_t DISPLAY_MISO_PIN = 1;
constexpr uint32_t DISPLAY_SPI_SPEED = 20000000;  // 20 MHz (see note below)

// Multiplexer pins (CD74HC4067)
constexpr uint8_t MUX_S0_PIN = 3;
constexpr uint8_t MUX_S1_PIN = 2;
constexpr uint8_t MUX_S2_PIN = 5;
constexpr uint8_t MUX_S3_PIN = 6;
constexpr uint8_t MUX_SIGNAL_PIN = 4;
constexpr uint8_t MUX_MAX_CHANNELS = 16;

// Timing
constexpr uint16_t MUX_DEBOUNCE_US = 20;   // Mux settle time
constexpr uint32_t PIN_DEBOUNCE_MS = 5;    // Direct pin debounce
```

#### USB Power Constraints

The hardware configuration is optimized for **USB bus power** (micro USB, ~250 mA max):

| Parameter | Default | Reduced | Reason |
|-----------|---------|---------|--------|
| CPU Frequency | 600 MHz | **450 MHz** | Current draw exceeds USB limit at 600 MHz |
| SPI Speed | 70 MHz | **20 MHz** | Reduces current spikes during display updates |

**Symptoms of power issues:**
- Random freezes or resets during display updates
- USB disconnections under load
- Instability when all encoders are active

**If using external power supply** (5V, >500 mA), you can increase these values in `platformio.ini`:

```ini
board_build.f_cpu = 600000000L  # Full speed with external power
```

And modify `DISPLAY_SPI_SPEED` in `System.hpp` (up to 70 MHz supported by ILI9341_T4).

### System::Display

Display and rendering configuration.

```cpp
constexpr uint16_t SCREEN_WIDTH = 320;
constexpr uint16_t SCREEN_HEIGHT = 240;
constexpr uint8_t ROTATION = 3;

// Memory buffers
constexpr size_t FRAMEBUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT;
constexpr size_t DIFFBUFFER_SIZE = 16384;  // 16KB
constexpr size_t LVGL_BUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT;

// Refresh
constexpr int REFRESH_RATE_HZ = 60;
constexpr uint32_t REFRESH_PERIOD_MS = 16;
```

### System::Midi

MIDI parameters.

```cpp
constexpr size_t MAX_ACTIVE_NOTES = 16;
constexpr size_t USB_SYSEX_MAX_SIZE = 16000;  // 16KB SysEx buffer
```

### System::Input

Input gesture timing.

```cpp
constexpr uint32_t LONG_PRESS_DEFAULT_MS = 500;
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 300;
constexpr uint32_t LATCH_THRESHOLD_MS = 300;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
```

---

## InputID.hpp

Strongly-typed identifiers for all hardware inputs.

### ButtonID

```cpp
enum class ButtonID : uint16_t {
    // Left navigation (10-19)
    LEFT_TOP = 10,
    LEFT_CENTER = 11,
    LEFT_BOTTOM = 12,

    // Bottom navigation (20-29)
    BOTTOM_LEFT = 20,
    BOTTOM_CENTER = 21,
    BOTTOM_RIGHT = 22,

    // Macro encoder buttons (30-39)
    MACRO_1 = 31,
    MACRO_2 = 32,
    // ... through MACRO_8 = 38

    // Special (40-49)
    NAV = 40,
};
```

### EncoderID

```cpp
enum class EncoderID : uint16_t {
    // Macro encoders (301-308)
    MACRO_1 = 301,
    MACRO_2 = 302,
    // ... through MACRO_8 = 308

    // Special encoders (400-499)
    NAV = 400,  // Relative mode, infinite rotation
    OPT = 410,  // High resolution (600 PPR)
};
```

### ID Correspondence Pattern

Button and encoder IDs follow a mathematical pattern:
- `ButtonID::MACRO_N` (30+N) ↔ `EncoderID::MACRO_N` (300+N)
- `ButtonID::NAV` (40) ↔ `EncoderID::NAV` (400)

---

## InputDefinition.hpp

Hardware definitions linking IDs to physical pins.

### Button Definition

```cpp
constexpr Hardware::Button BUTTONS[] = {
    // {ButtonID, GpioPin}
    {ButtonID::LEFT_TOP, muxPin(9)},      // Multiplexer channel 9
    {ButtonID::LEFT_CENTER, muxPin(10)},
    {ButtonID::NAV, mcuPin(32)},          // Direct MCU pin 32
    {ButtonID::MACRO_1, muxPin(7)},
    // ...
};
```

### Encoder Definition

```cpp
constexpr Hardware::Encoder ENCODERS[] = {
    // {EncoderID, pinA, pinB, ppr, stepsPerDetent, mode}
    {EncoderID::MACRO_1, mcuPin(22), mcuPin(23)},  // Defaults: 24 PPR, 1 step, Absolute
    {EncoderID::NAV, mcuPin(31), mcuPin(30), 24, 4, Hardware::EncoderMode::Relative},
    {EncoderID::OPT, mcuPin(34), mcuPin(33), 600, 1},  // High resolution
};
```

### Pin Helpers

```cpp
mcuPin(n)           // Direct MCU pin
mcuPin(n, PinMode)  // With pull mode
muxPin(n)           // Multiplexer channel 0-15
```

### Adding New Inputs

1. Add ID to `InputID.hpp`
2. Add hardware definition to `InputDefinition.hpp`
3. Optionally add MIDI mapping to `MidiMapping.hpp`

---

## Version.hpp

Version information for Core and API.

```cpp
namespace Core {
    constexpr uint8_t VERSION_MAJOR = 1;
    constexpr uint8_t VERSION_MINOR = 0;
    constexpr uint8_t VERSION_PATCH = 0;
    constexpr const char* VERSION = "1.0.0-beta.1";
    constexpr bool IS_PRERELEASE = true;
}

namespace API {
    constexpr uint8_t VERSION_MAJOR = 1;
    constexpr uint8_t VERSION_MINOR = 0;
    constexpr uint8_t VERSION_PATCH = 0;
}
```

### Prerelease Configuration

```cpp
// For prerelease (beta, rc):
#define CORE_IS_PRERELEASE
#define CORE_VERSION_PRERELEASE "beta.1"
// Result: "1.0.0-beta.1"

// For release: comment out CORE_IS_PRERELEASE
// Result: "1.0.0"
```

---

## See Also

- [Architecture](../../docs/ARCHITECTURE.md) — System design
- [Core Types](../core/Type.hpp) — GpioPin, PinMode structs
