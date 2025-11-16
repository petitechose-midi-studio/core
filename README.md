# MIDI Studio - Core Library

**PlatformIO library for building MIDI controllers on Teensy 4.1**

Core is a **library**, not standalone firmware. Plugins reference it via `lib_deps` and provide their own `main.cpp`.

---

## What is Core?

Core provides the foundation for building MIDI Studio plugins:

- **Hardware Abstraction** - Display, encoders, buttons, multiplexer drivers
- **UI Framework** - LVGL-based components (ViewContainer, ViewManager, Registry)
- **Plugin System** - Callback-based plugin registration
- **Event Bus** - Unified event system for component communication
- **Protocol Tools** - Python code generator for C++/Java protocol classes
- **Resource Management** - Font rendering, assets, UI components

**Core is NOT a standalone firmware** - plugins provide the main entry point and register themselves via callback.c

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

### Testing Core (without plugins)

Core includes a minimal example for testing:

```bash
git clone https://github.com/petitechose-midi-studio/core.git
cd core/examples/minimal

# Build and upload
pio run -e prod -t upload

# Monitor serial output
pio device monitor
```

This builds core with an empty plugin callback (no plugins loaded).

### Using Core in Your Plugin

**1. Reference core in your platformio.ini:**

```ini
[env]
platform = teensy
board = teensy41
framework = arduino

lib_deps =
    https://github.com/petitechose-midi-studio/core.git#v1.0.0

build_flags =
    -D USB_MIDI_SERIAL
    -D LV_CONF_INCLUDE_SIMPLE
    -D LV_LVGL_H_INCLUDE_SIMPLE
```

**2. Create src/main.cpp with callback:**

```cpp
#include <Arduino.h>
#include "app/MidiStudioApp.hpp"
#include "plugin/myplugin/Plugin.hpp"

void setupPlugins(PluginManager& manager) {
    manager.registerPlugin<Plugin::MyPlugin::Plugin>("myplugin");
}

MidiStudioApp app(setupPlugins);

void setup() {}
void loop() { app.update(); }
```

---

## Repository Structure

```
core/
├── src/                     # Library source code (all compilable code)
│   ├── adapter/            # Hardware drivers (display, input, MIDI)
│   ├── api/                # Plugin API (ControllerAPI)
│   ├── app/                # MidiStudioApp main class
│   ├── config/             # Configuration (Version, System)
│   ├── core/               # Core systems (events, factories)
│   ├── manager/            # Managers (Plugin, View, Input)
│   ├── resource/           # UI resources (fonts, widgets, themes)
│   └── ui/                 # UI controllers and views
├── resource/               # Non-compiled resources
│   ├── code/py/protocol/  # Python protocol generator
│   ├── font/              # Source fonts (TTF)
│   ├── img/               # Source images
│   └── script/            # Build scripts
├── examples/
│   └── minimal/           # Test core without plugins
│       ├── platformio.ini
│       └── src/main.cpp
├── library.json            # PlatformIO library metadata
├── library.properties      # Arduino library metadata
├── LICENSE                 # CC-BY-NC-SA 4.0
└── README.md
```

---

## Local Development

For developing both core and a plugin simultaneously:

**Option 1: File path reference**

```ini
# plugin/platformio.ini
lib_deps =
    file://../core
```

**Option 2: lib_extra_dirs (recommended)**

```ini
# plugin/platformio.ini
lib_extra_dirs = ../../..
lib_ldf_mode = deep+

lib_deps =
    petitechose-midi-studio-core
```

See `examples/minimal/platformio.ini` for a complete working example.

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

**1. Implement IPlugin interface:**

```cpp
#include "resource/common/interface/IPlugin.hpp"
#include "api/ControllerAPI.hpp"

namespace Plugin::MyPlugin {

class Plugin : public IPlugin {
public:
    explicit Plugin(ControllerAPI& api) : api_(api), enabled_(true) {}

    bool initialize() override {
        // Setup your UI and handlers
        return true;
    }

    void cleanup() override {}
    const char* getName() const override { return "My Plugin"; }
    bool isEnabled() const override { return enabled_; }
    void setEnabled(bool enabled) override { enabled_ = enabled; }
    void update() override {}

private:
    ControllerAPI& api_;
    bool enabled_;
};

} // namespace Plugin::MyPlugin
```

**2. Register in main.cpp:**

```cpp
#include "app/MidiStudioApp.hpp"
#include "plugin/myplugin/Plugin.hpp"

void setupPlugins(PluginManager& manager) {
    manager.registerPlugin<Plugin::MyPlugin::Plugin>("myplugin");
}

MidiStudioApp app(setupPlugins);
void setup() {}
void loop() { app.update(); }
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

## Build Configuration

Required build flags for plugins using core:

```ini
build_flags =
    -D USB_MIDI_SERIAL              # USB MIDI + Serial
    -D TEENSY_OPT_SMALLEST_CODE     # Size optimization
    -D LV_CONF_INCLUDE_SIMPLE       # LVGL config
    -D LV_LVGL_H_INCLUDE_SIMPLE     # LVGL header

board_build.f_cpu = 520000000       # CPU: 520 MHz
```

### Display Pinout (ILI9341)

- CS = Pin 28, DC = Pin 29, RST = Pin 30
- MOSI = Pin 26, SCK = Pin 27, MISO = Pin 1
- Speed: 70 MHz SPI

---

## Versioning

Core follows **Semantic Versioning** (SemVer):

- **Core Version** (MAJOR.MINOR.PATCH): Framework implementation
  - MAJOR: Breaking structural changes
  - MINOR: New features (backward-compatible)
  - PATCH: Bug fixes

- **API Version** (MAJOR.MINOR.PATCH): Plugin compatibility
  - MAJOR: Breaking API changes (plugins must update)
  - MINOR: New API features (backward-compatible)
  - PATCH: API bug fixes

Lock to specific versions in production:

```ini
lib_deps =
    https://github.com/petitechose-midi-studio/core.git#v1.0.0
```

**Current Version:** See `src/config/Version.hpp`

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
