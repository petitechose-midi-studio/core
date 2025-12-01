# MIDI Studio Core

**Framework for MIDI controllers on Teensy 4.1 with LVGL display**

[![Version](https://img.shields.io/badge/version-1.0.0--beta.1-blue)]()
[![License](https://img.shields.io/badge/license-CC--BY--NC--SA--4.0-green)]()
[![Platform](https://img.shields.io/badge/platform-Teensy%204.1-orange)]()

---

## What is Core?

Core is a PlatformIO library providing:

- **Hardware Abstraction** — ILI9341 display, encoders, buttons, multiplexer
- **Event Bus** — Decoupled component communication
- **Plugin System** — Extensible architecture for DAW integrations
- **UI Framework** — Optimized LVGL components (widgets, theme, views)

→ [Detailed architecture](docs/ARCHITECTURE.md)

---

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | Teensy 4.1 @ 450 MHz¹ |
| Display | ILI9341 320×240 SPI @ 20 MHz² |
| Encoders | 10× (8 macro + NAV + OPT) |
| Buttons | 14× (6 nav + 8 macro) via CD74HC4067 |

> ¹ **CPU underclocked from 600 MHz**: Micro USB power delivery is limited to ~250 mA. At 600 MHz with display + encoders active, current draw can exceed this limit causing instability. 450 MHz provides stable operation within USB power budget.
>
> ² **SPI underclocked from 70 MHz**: Same power constraints. Lower SPI frequency reduces current spikes during display updates, improving stability on USB power.

→ Pin configuration: [src/config/](src/config/)

---

## Quick Start

→ **First time?** See [Getting Started](docs/GETTING_STARTED.md) for prerequisites (VS Code, PlatformIO, etc.)

### Plugin Mode (Production)

```ini
# platformio.ini
[env]
platform = teensy
board = teensy41
framework = arduino
board_build.f_cpu = 450000000L

lib_deps =
    https://github.com/petitechose-midi-studio/core.git#v1.0.0

build_flags =
    -D USB_MIDI_SERIAL
    -D LV_CONF_INCLUDE_SIMPLE
    -D LV_LVGL_H_INCLUDE_SIMPLE
```

```cpp
// main.cpp
#include "app/MidiStudioApp.hpp"

void setupPlugins(PluginManager& mgr) {
    mgr.registerPlugin<MyPlugin>("myplugin");
}

MidiStudioApp app(setupPlugins);

void setup() { app.setup(); }
void loop() { app.update(); }
```

→ [Full plugin guide](docs/PLUGIN_DEVELOPMENT.md)

### Standalone Mode (Development)

```bash
git clone https://github.com/petitechose-midi-studio/core.git
cd core
pio run -e debug -t upload
pio device monitor
```

---

## Documentation

| Document | Content |
|----------|---------|
| [Getting Started](docs/GETTING_STARTED.md) | Prerequisites, installation, first build |
| [Architecture](docs/ARCHITECTURE.md) | Layers, patterns, EventBus, boot sequence |
| [Code Style](docs/CODE_STYLE.md) | Naming conventions, formatting |
| [Plugin Development](docs/PLUGIN_DEVELOPMENT.md) | API, lifecycle, examples |
| [src/api/](src/api/) | ControllerAPI reference |
| [src/config/](src/config/) | System & hardware configuration |
| [src/core/](src/core/) | EventBus, types, factories |
| [src/ui/shared/](src/ui/shared/) | Widgets, theme, UI components |

---

## Structure

```
core/
├── src/
│   ├── adapter/       # Hardware drivers (display, input, MIDI)
│   ├── api/           # ControllerAPI (plugin interface)
│   ├── app/           # MidiStudioApp (composition root)
│   ├── boot/          # Boot sequence manager
│   ├── config/        # System configuration (pins, timing)
│   ├── core/          # EventBus, types, factories
│   ├── manager/       # Plugin, View, Input managers
│   └── ui/            # Views, widgets, theme
├── docs/              # Documentation
└── script/            # Build scripts (fonts, MIDI patch)
```

---

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [ILI9341_T4](https://github.com/vindar/ILI9341_T4) | ^1.6.0 | Optimized Teensy display driver |
| [LVGL](https://lvgl.io/) | ^9.4.0 | UI framework |
| [EncoderTool](https://github.com/luni64/EncoderTool) | latest | Interrupt-based encoder handling |

---

## Version

| Component | Version |
|-----------|---------|
| Core | 1.0.0-beta.1 |
| API | 1.0.0 |

→ [Versioning policy](docs/ARCHITECTURE.md#versioning)

---

## License

[CC-BY-NC-SA 4.0](LICENSE) — Non-commercial use only.

**You may:**
- Use for personal/educational projects
- Modify and share (same license)

**You must:**
- Attribute to petitechose.audio
- Share under CC-BY-NC-SA 4.0

**You may not:**
- Commercial use without permission

---

## Links

- [Hardware Design](https://github.com/petitechose-midi-studio/hardware) — PCB, BOM, enclosure
- [Bitwig Plugin](https://github.com/petitechose-midi-studio/plugin-bitwig) — Bitwig Studio integration
- [Issues](https://github.com/petitechose-midi-studio/core/issues) — Bug reports
- [Commercial](mailto:contact@petitechose.audio) — Commercial licensing

---

**Built by [petitechose.audio](https://petitechose.audio)**
