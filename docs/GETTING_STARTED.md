# Getting Started

Prerequisites and setup guide for developing with MIDI Studio Core.

---

## Prerequisites

### Required Software

| Software | Version | Purpose |
|----------|---------|---------|
| [VS Code](https://code.visualstudio.com/) | Latest | IDE |
| [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) | Latest | Build system extension |
| [Git](https://git-scm.com/) | Latest | Version control |

### Optional

| Software | Purpose |
|----------|---------|
| [Python 3.8+](https://www.python.org/) | Build scripts (font conversion, SysEx patch) |
| Serial terminal | Debugging (PlatformIO has built-in monitor) |

---

## Installation

### 1. Install VS Code

Download and install from [code.visualstudio.com](https://code.visualstudio.com/).

### 2. Install PlatformIO Extension

1. Open VS Code
2. Go to Extensions (`Ctrl+Shift+X` / `Cmd+Shift+X`)
3. Search for "PlatformIO IDE"
4. Click **Install**
5. Restart VS Code when prompted

### 3. Verify Installation

1. Open Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`)
2. Type "PlatformIO: Home"
3. PlatformIO Home should open in a new tab

---

## Project Setup

### Clone Core Repository (Standalone Development)

```bash
git clone https://github.com/petitechose-midi-studio/core.git
cd core
```

### Open in VS Code

```bash
code .
```

Or: File → Open Folder → select `core/`

### First Build

1. Wait for PlatformIO to initialize (first time may take a few minutes)
2. Click the **PlatformIO** icon in the sidebar (alien head)
3. Under **PROJECT TASKS** → **prod** → click **Build**

Or use the terminal:

```bash
pio run -e prod
```

### Upload to Hardware

1. Connect Teensy 4.1 via USB
2. Click **Upload** in PlatformIO sidebar

Or:

```bash
pio run -e prod -t upload
```

### Monitor Serial Output

```bash
pio device monitor
```

Or click **Monitor** in PlatformIO sidebar.

---

## Project Environments

| Environment | Purpose | Build Flags |
|-------------|---------|-------------|
| `prod` | Production build | Optimized |
| `debug` | Development build | `-DDEBUG_LOGS` enabled |

### Build Debug Version

```bash
pio run -e debug -t upload
```

Debug build enables serial logging via `LOGLN()`, `LOGF()` macros.

---

## Creating a Plugin Project

### 1. Create Project Structure

```bash
mkdir my-plugin
cd my-plugin
```

### 2. Create platformio.ini

```ini
[platformio]
default_envs = prod

[env]
framework = arduino
platform = teensy
board = teensy41
monitor_dtr = 0
monitor_rts = 0
board_build.f_cpu = 450000000L

build_flags =
    -D USB_MIDI_SERIAL
    -D TEENSY_OPT_SMALLEST_CODE
    -D LV_CONF_INCLUDE_SIMPLE
    -D LV_LVGL_H_INCLUDE_SIMPLE
    -I src

lib_deps =
    https://github.com/petitechose-midi-studio/core.git#v1.0.0

[env:prod]
build_flags =
    ${env.build_flags}

[env:debug]
build_flags =
    ${env.build_flags}
    -DDEBUG_LOGS
```

### 3. Create src/main.cpp

```cpp
#include "app/MidiStudioApp.hpp"
#include "plugin/MyPlugin.hpp"

void setupPlugins(PluginManager& manager) {
    manager.registerPlugin<MyPlugin>("myplugin");
}

MidiStudioApp app(setupPlugins);

void setup() {
    app.setup();
}

void loop() {
    app.update();
}
```

### 4. Create Plugin Class

See [Plugin Development Guide](PLUGIN_DEVELOPMENT.md) for complete instructions.

---

## VS Code Configuration

### Recommended Extensions

| Extension | Purpose |
|-----------|---------|
| PlatformIO IDE | Build, upload, monitor |
| C/C++ | IntelliSense, debugging |
| clangd | Better C++ completion (optional) |

### Workspace Settings

Create `.vscode/settings.json`:

```json
{
    "editor.tabSize": 4,
    "editor.insertSpaces": true,
    "editor.formatOnSave": true,
    "files.trimTrailingWhitespace": true,
    "files.insertFinalNewline": true,
    "C_Cpp.default.configurationProvider": "platformio.platformio-ide"
}
```

### IntelliSense Configuration

PlatformIO generates `c_cpp_properties.json` automatically. If IntelliSense doesn't work:

1. Build the project once (`pio run`)
2. Restart VS Code
3. PlatformIO will configure IntelliSense paths

---

## Hardware Setup

### Teensy 4.1 Connection

1. Connect Teensy via **micro USB** cable
2. Ensure cable supports data (not charge-only)
3. Teensy should appear as a serial device

### First-Time Teensy Setup

If this is your first time using Teensy:

1. Download [Teensy Loader](https://www.pjrc.com/teensy/loader.html)
2. Run the loader once to install drivers (Windows)
3. PlatformIO will use Teensy Loader automatically

### Troubleshooting Connection

| Issue | Solution |
|-------|----------|
| Teensy not detected | Try different USB port/cable |
| Upload fails | Press button on Teensy to enter bootloader |
| Serial monitor empty | Ensure `monitor_dtr = 0` in platformio.ini |
| Random resets | USB power issue, see [Power Constraints](../src/config/README.md#usb-power-constraints) |

---

## Build Errors

### Common Issues

#### "lvgl.h not found"

```
fatal error: lvgl.h: No such file or directory
```

**Solution**: Ensure build flags include:
```ini
-D LV_CONF_INCLUDE_SIMPLE
-D LV_LVGL_H_INCLUDE_SIMPLE
```

#### "lv_conf.h not found"

**Solution**: Add include path:
```ini
-I src/config/ui
```

#### Linker errors with LVGL

**Solution**: Ensure Core is properly referenced:
```ini
lib_deps =
    https://github.com/petitechose-midi-studio/core.git#v1.0.0
```

#### Out of memory

**Solution**: Enable size optimization:
```ini
-D TEENSY_OPT_SMALLEST_CODE
```

---

## Development Workflow

### Typical Development Cycle

1. **Edit code** in VS Code
2. **Build** (`Ctrl+Alt+B` or `pio run`)
3. **Upload** (`Ctrl+Alt+U` or `pio run -t upload`)
4. **Monitor** serial output (`pio device monitor`)
5. **Iterate**

### Keyboard Shortcuts (PlatformIO)

| Action | Shortcut |
|--------|----------|
| Build | `Ctrl+Alt+B` |
| Upload | `Ctrl+Alt+U` |
| Clean | `Ctrl+Alt+C` |
| Serial Monitor | `Ctrl+Alt+S` |

---

## Next Steps

- [Architecture](ARCHITECTURE.md) — Understand the system design
- [Plugin Development](PLUGIN_DEVELOPMENT.md) — Create your first plugin
- [Code Style](CODE_STYLE.md) — Follow coding conventions
- [ControllerAPI](../src/api/README.md) — API reference

---

## Getting Help

- [GitHub Issues](https://github.com/petitechose-midi-studio/core/issues) — Bug reports
- [PlatformIO Docs](https://docs.platformio.org/) — Build system reference
- [LVGL Docs](https://docs.lvgl.io/) — UI framework reference
- [Teensy Forums](https://forum.pjrc.com/) — Hardware questions
