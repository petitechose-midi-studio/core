# MIDI Studio - Core Firmware

**Teensy 4.1 firmware for MIDI Studio controller - Core framework with UI components and plugin API**

This repository contains the standalone core firmware that provides the foundation for MIDI Studio plugins.

---

## Overview

The core firmware provides:

- **Hardware Abstraction** - Display, encoders, buttons, multiplexer drivers
- **UI Framework** - LVGL-based components (ViewContainer, ViewManager, Registry)
- **Plugin API** - Clean interface for DAW integration plugins
- **Protocol Generator** - Python tools for generating C++ protocol code
- **Resource Management** - Font rendering, assets, UI components

Plugins (like Bitwig Studio integration) are **separate repositories** that depend on this core via git submodules.

---

## Hardware Requirements

- **MCU:** Teensy 4.1 (ARM Cortex-M7 @ 600 MHz)
- **RAM:** 8 MB PSRAM (**mandatory** - soldered on underside pads)
- **Display:** ILI9341 2.8" TFT (320x240, SPI 70 MHz)
- **Encoders:** 10x rotary encoders (various PPR)
- **Buttons:** 15x tactile buttons
- **Multiplexer:** CD74HC4067 (16-channel)

See [hardware repo](https://github.com/petitechose-midi-studio/hardware) for PCB schematics, BOM, and assembly guide.

---

## Software Requirements

### Development Tools

- **PlatformIO CLI or IDE** (latest)
- **Python 3.8+** (for protocol generator)
- **Git** (for submodules)

### Dependencies (auto-installed by PlatformIO)

- ILI9341_T4 @ ^1.6.0
- lvgl @ ^9.4.0
- Embedded Template Library @ ^20.39.4
- CD74HC4067 @ ^1.0.2
- Bounce2 @ ^2.72
- EncoderTool (luni64)

---

## Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/petitechose-midi-studio/core.git
cd core
```

### 2. Build the Firmware

```bash
# Production build (smallest code size)
pio run -e prod

# Debug build (with serial logging)
pio run -e debug
```

### 3. Upload to Teensy 4.1

```bash
# Production
pio run -e prod -t upload

# Debug
pio run -e debug -t upload
```

### 4. Monitor Serial Output (Debug Only)

```bash
pio device monitor
```

---

## Repository Structure

```
core/
   src/                    # Core firmware source code
      adapter/            # Hardware drivers
         display/        # ILI9341 display driver
         input/          # Encoder and button drivers
         midi/           # USB MIDI communication
         multiplexer/    # CD74HC4067 multiplexer driver
      app/                # Application entry point
      config/             # Configuration and registry
      core/               # Core systems (plugin manager, etc.)
      main.cpp            # Firmware entry point
   api/                    # Plugin API
      cpp/
          ControllerAPI.hpp  # Public API for plugins
   resources/              # Assets and tools
      code/
         cpp/common/     # Shared UI components
         py/protocol/    # Protocol generator (Python)
      font/               # Font files (Inter, JetBrains Mono)
      script/             # Build scripts
   platformio.ini          # PlatformIO configuration
   README.md               # This file
   LICENSE                 # CC-BY-NC-SA 4.0
```

---

## Building with Plugins

**Core firmware alone does NOT include any DAW plugins.** To build with a plugin:

1. **Clone a plugin repository** (e.g., plugin-bitwig)
2. **The plugin repo will include core as a submodule**
3. **Build from the plugin repo** (it will compile core + plugin together)

Example:

```bash
# Clone plugin repo (includes core as submodule)
git clone --recursive https://github.com/petitechose-midi-studio/plugin-bitwig.git
cd plugin-bitwig

# Build (compiles core + plugin)
pio run -e prod -t upload
```

---

## Plugin API

### ControllerAPI Interface

Plugins interact with the controller via the `ControllerAPI` interface:

**Location:** `api/cpp/ControllerAPI.hpp`

**Key Methods:**

```cpp
class ControllerAPI {
public:
    // Input Events
    void onPressed(uint8_t id);
    void onReleased(uint8_t id);
    void onTurned(uint8_t id, int8_t delta);

    // Output
    void sendSysEx(const uint8_t* data, uint16_t size);

    // Display Access
    lv_obj_t* getRootScreen();
    lv_obj_t* createView();
};
```

### Creating a Plugin

See [plugin-bitwig](https://github.com/petitechose-midi-studio/plugin-bitwig) for a complete example.

**Minimal Plugin:**

```cpp
#include "common/interface/IPlugin.hpp"
#include "ControllerAPI.hpp"

class MyPlugin : public IPlugin {
public:
    explicit MyPlugin(ControllerAPI& api) : api_(api) {}

    bool initialize() override {
        // Setup your UI and handlers
        return true;
    }

    const char* getName() const override { return "My Plugin"; }

private:
    ControllerAPI& api_;
};
```

**Register in PluginRegistry:**

```cpp
// In plugin repo's PluginRegistry.cpp
void PluginRegistry::setup(PluginManager& manager) {
    manager.registerPlugin<MyPlugin>("myplugin");
}
```

---

## Protocol Generator

The `resource/code/py/protocol/` directory contains a Python code generator that creates C++ and Java protocol classes from message definitions.

**Usage:**

```bash
cd resources/code/py/protocol
python generate.py
```

This generates type-safe message classes for communication between firmware and DAW extensions.

---

## Configuration

### Display Pinout (ILI9341)

- CS = Pin 28
- DC = Pin 29
- RST = Pin 30
- MOSI = Pin 26
- SCK = Pin 27
- MISO = Pin 1
- Speed: 70 MHz

### Build Options

Edit `platformio.ini` to customize:

- **CPU Speed:** `board_build.f_cpu = 520000000` (520 MHz)
- **USB Mode:** `-D USB_MIDI_SERIAL` (USB MIDI + Serial)
- **Optimization:** `-D TEENSY_OPT_SMALLEST_CODE`

---

## Versioning

Core firmware follows **Semantic Versioning** (SemVer):

- **MAJOR:** Breaking changes to ControllerAPI
- **MINOR:** New features (backward-compatible)
- **PATCH:** Bug fixes

**Current Version:** See `src/config/System.hpp`

---

## Related Repositories

- **[Hardware Design](https://github.com/petitechose-midi-studio/hardware)** - PCB, enclosure, BOM
- **[Bitwig Plugin](https://github.com/petitechose-midi-studio/plugin-bitwig)** - Bitwig Studio integration

---

## License

Licensed under **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC-BY-NC-SA 4.0)**.

### What This Means

**You MAY:**
- Use for **personal/educational projects**
- Modify and improve the code
- Share modifications (same license)

**You MUST:**
- Provide attribution to petitechose.audio
- Share modifications under CC-BY-NC-SA 4.0
- Indicate if changes were made

**You MAY NOT:**
- Use for **commercial purposes** without permission

See [LICENSE](LICENSE) for full terms.

---

## Support

**Issues:** GitHub Issues

**Commercial Licensing:** <contact@petitechose.audio>

---

**Built by petitechose.audio**
