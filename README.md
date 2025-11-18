# MIDI Studio - Core Library

**PlatformIO library for building MIDI controllers on Teensy 4.1**

Core is designed as a **library** for plugin development. Plugins reference it via `lib_deps` and provide their own `main.cpp`. Core can also be built standalone for testing and development purposes.

---

## What is Core?

Core provides the foundation for building MIDI Studio plugins:

- **Hardware Abstraction** - Display, encoders, buttons, multiplexer drivers
- **UI Framework** - LVGL-based components (ViewContainer, ViewManager, Registry)
- **Plugin System** - Callback-based plugin registration
- **Event Bus** - Unified event system for component communication
- **Resource Management** - Font rendering, assets, UI components

---

## Core Architecture: Standalone & Library

Core has a dual nature:

**1. Library Mode (Production)**
- Plugins reference core via PlatformIO `lib_deps` pointing to GitHub releases
- `library.json` exports only library code (`srcFilter` excludes `main.cpp`)
- Plugins provide their own `main.cpp` with plugin registration
- **Recommended approach** for plugin development

**2. Standalone Mode (Development/Testing)**
- Build directly from root using `platformio.ini`
- `src/main.cpp` provides entry point with no plugins loaded
- Useful for testing core features and hardware during development

This design allows core development independently while being consumed as a clean library.

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
- **Python 3.8+** (for build scripts)
- **Git** (for dependencies)

### Dependencies (auto-installed by PlatformIO)

- ILI9341_T4 @ ^1.6.0
- lvgl @ ^9.4.0
- Embedded Template Library @ ^20.39.4
- CD74HC4067 @ ^1.0.2
- Bounce2 @ ^2.72
- EncoderTool (luni64)

---

## Quick Start

### Using Core in Your Plugin (Recommended)

**1. Reference core release in your platformio.ini:**

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

> Always pin to a specific release tag (e.g., `#v1.0.0`) for production stability.

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

### Testing Core in Standalone Mode

For testing core features without creating a plugin:

```bash
git clone https://github.com/petitechose-midi-studio/core.git
cd core

# Build and upload (standalone mode with no plugins)
pio run -e prod -t upload

# Monitor serial output
pio device monitor
```

This uses the root [platformio.ini](platformio.ini) and [src/main.cpp](src/main.cpp) to run core with an empty plugin callback.

---

## Repository Structure

```
core/
├── platformio.ini          # Standalone build configuration
├── library.json            # PlatformIO library metadata & export config
├── src/                    # Library source code (all compilable code)
│   ├── main.cpp           # Standalone entry point (excluded from library export)
│   ├── adapter/           # Hardware drivers (display, input, MIDI)
│   ├── api/               # Plugin API (ControllerAPI)
│   ├── app/               # MidiStudioApp main class
│   ├── config/            # Configuration (Version, System)
│   ├── core/              # Core systems (events, factories)
│   ├── log/               # Logging utilities
│   ├── manager/           # Managers (Plugin, View, Input)
│   ├── resource/          # UI resources (fonts, widgets, themes)
│   └── ui/                # UI controllers and views
├── script/                # Build scripts (Python)
├── resource/              # Non-compiled resources
│   └── font/              # Source fonts (TTF)
├── LICENSE                # CC-BY-NC-SA 4.0
└── README.md
```

---

## Local Development

### Developing Core in Standalone Mode

```bash
cd core

# Build with debug logs enabled
pio run -e debug

# Upload to hardware
pio run -e debug -t upload
```

### Temporary Local Core Usage in Plugins

**For temporary testing only** - when developing both core and a plugin simultaneously:

**Option 1: File path reference**

```ini
# plugin/platformio.ini
lib_deps =
    file://../core  # Temporary: use for local testing only
```

**Option 2: lib_extra_dirs**

```ini
# plugin/platformio.ini
lib_extra_dirs = ../../..
lib_deps =
    petitechose-midi-studio-core  # Local resolution
```

> **Important:** Always replace local references with GitHub release tags before committing:
> ```ini
> lib_deps =
>     https://github.com/petitechose-midi-studio/core.git#v1.0.0
> ```

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

**Always pin to specific release tags in production:**

```ini
lib_deps =
    https://github.com/petitechose-midi-studio/core.git#v1.0.0
```

**Never use:**
- Branch references like `#main` or `#develop` (unstable)
- Local file paths like `file://../core` (not portable)

**Current Version:** See [src/config/Version.hpp](src/config/Version.hpp)

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
