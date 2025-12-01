# Plugin Development Guide

This guide explains how to create plugins for MIDI Studio Core.

---

## Overview

Plugins extend Core with DAW-specific integrations (Bitwig, Ableton, etc.). Each plugin:

- Implements the `IPlugin` interface
- Receives a `ControllerAPI` for hardware interaction
- Creates its own LVGL views on the plugin screen
- Handles MIDI communication with the host DAW

---

## Project Setup

### 1. Create Plugin Repository

```
my-plugin/
├── platformio.ini
├── src/
│   ├── main.cpp
│   └── plugin/
│       └── myplugin/
│           ├── Plugin.hpp
│           ├── Plugin.cpp
│           └── view/
│               └── MainView.hpp
└── README.md
```

### 2. Configure platformio.ini

```ini
[env]
platform = teensy
board = teensy41
framework = arduino
board_build.f_cpu = 450000000L

lib_deps =
    https://github.com/petitechose-midi-studio/core.git#v1.0.0

build_flags =
    -D USB_MIDI_SERIAL
    -D TEENSY_OPT_SMALLEST_CODE
    -D LV_CONF_INCLUDE_SIMPLE
    -D LV_LVGL_H_INCLUDE_SIMPLE
    -I src

extra_scripts = pre:script/midi/sysex/patch_usb_midi_sysex.py
```

> **Important**: Always pin to a specific release tag (`#v1.0.0`), never use `#main`.

### 3. Create main.cpp

```cpp
#include "app/MidiStudioApp.hpp"
#include "plugin/myplugin/Plugin.hpp"

void setupPlugins(PluginManager& manager) {
    manager.registerPlugin<Plugin::MyPlugin::Plugin>("myplugin");
}

MidiStudioApp app(setupPlugins);

void setup() {
    app.setup();
}

void loop() {
    app.update();
}
```

---

## IPlugin Interface

Every plugin must implement `IPlugin`:

```cpp
// resource/common/interface/IPlugin.hpp

class IPlugin {
public:
    virtual ~IPlugin() = default;

    /**
     * @brief Initialize plugin after registration
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;

    /**
     * @brief Cleanup before destruction
     */
    virtual void cleanup() = 0;

    /**
     * @brief Called every frame in main loop
     */
    virtual void update() = 0;

    /**
     * @brief Get plugin display name
     */
    virtual const char* getName() const = 0;

    /**
     * @brief Check if plugin is enabled
     */
    virtual bool isEnabled() const = 0;

    /**
     * @brief Enable/disable plugin
     */
    virtual void setEnabled(bool enabled) = 0;
};
```

---

## Plugin Implementation

### Basic Structure

```cpp
// plugin/myplugin/Plugin.hpp
#pragma once

#include "resource/common/interface/IPlugin.hpp"
#include "api/ControllerAPI.hpp"

namespace Plugin::MyPlugin {

class Plugin : public IPlugin
{
public:
    explicit Plugin(ControllerAPI& api);
    ~Plugin() override;

    // IPlugin interface
    bool initialize() override;
    void cleanup() override;
    void update() override;
    const char* getName() const override { return "My Plugin"; }
    bool isEnabled() const override { return enabled_; }
    void setEnabled(bool enabled) override { enabled_ = enabled; }

private:
    ControllerAPI& api_;
    bool enabled_ = true;

    // Your views, state, etc.
    std::unique_ptr<MainView> main_view_;
};

} // namespace Plugin::MyPlugin
```

### Implementation

```cpp
// plugin/myplugin/Plugin.cpp
#include "Plugin.hpp"
#include "view/MainView.hpp"

namespace Plugin::MyPlugin {

Plugin::Plugin(ControllerAPI& api)
    : api_(api) {}

Plugin::~Plugin() = default;

bool Plugin::initialize() {
    api_.log("MyPlugin initializing...");

    // Create main view on plugin screen
    main_view_ = std::make_unique<MainView>(
        api_.getParentContainer(),
        api_
    );

    // Show plugin view
    api_.showPluginView(*main_view_);

    // Setup global input bindings
    setupInputBindings();

    api_.log("MyPlugin initialized");
    return true;
}

void Plugin::cleanup() {
    api_.hidePluginView();
    main_view_.reset();
}

void Plugin::update() {
    if (!enabled_) return;

    // Per-frame update logic
    if (main_view_) {
        main_view_->update();
    }
}

void Plugin::setupInputBindings() {
    // Global binding: always active
    api_.onPressed(ButtonID::BOTTOM_LEFT, [this]() {
        navigateBack();
    });

    // Global encoder for navigation
    api_.onTurned(EncoderID::NAV, [this](float delta) {
        handleNavigation(delta);
    });
}

} // namespace Plugin::MyPlugin
```

---

## ControllerAPI Reference

### Input Binding

#### Button Events

```cpp
// Simple press/release
api.onPressed(ButtonID::MACRO_1, []() { /* action */ });
api.onReleased(ButtonID::MACRO_1, []() { /* action */ });

// Long press (default 500ms, customizable)
api.onLongPress(ButtonID::MACRO_1, []() { /* action */ });
api.onLongPress(ButtonID::MACRO_1, []() { /* action */ }, 1000); // 1 second

// Double tap
api.onDoubleTap(ButtonID::MACRO_1, []() { /* action */ });

// Button combo (both pressed)
api.onCombo(ButtonID::MACRO_1, ButtonID::MACRO_2, []() { /* action */ });
```

#### Encoder Events

```cpp
// Encoder turn (receives normalized 0.0-1.0 for Absolute, delta for Relative)
api.onTurned(EncoderID::MACRO_1, [](float value) {
    // value is 0.0-1.0 in Absolute mode
    // value is ±delta in Relative mode
});

// Encoder turn while button held
api.onTurnedWhilePressed(EncoderID::MACRO_1, ButtonID::MACRO_1, [](float value) {
    // Fine adjustment while button pressed
});
```

#### Scoped Bindings

Bindings can be scoped to LVGL objects - only active when the object is visible:

```cpp
// Only active when overlay_container is visible
api.onTurned(EncoderID::NAV, [this](float delta) {
    scrollList(delta);
}, overlay_container_);

// With latch behavior (tap=toggle, hold=momentary)
api.onPressed(ButtonID::MACRO_1, [this]() {
    toggleMode();
}, my_view_container_, true);  // latch=true
```

**Priority**: Scoped bindings take precedence over global bindings.

```cpp
// Clear all bindings for a scope (call in view destructor)
api.clearScope(my_container_);
```

#### Latch State

```cpp
// Check if button is latched
if (api.isLatched(ButtonID::MACRO_1)) {
    // Button is in latched ON state
}

// Manually set latch state (sync with external state)
api.setLatch(ButtonID::MACRO_1, true);
```

### MIDI Output

```cpp
// Control Change
api.sendCC(channel, ccNumber, value);  // channel 0-15, cc/value 0-127

// Notes
api.sendNoteOn(channel, note, velocity);
api.sendNoteOff(channel, note, velocity);

// SysEx
uint8_t sysex[] = {0xF0, 0x00, 0x01, 0x02, ..., 0xF7};
api.sendSysEx(sysex, sizeof(sysex));
```

### MIDI Input

```cpp
// React to incoming CC
api.onCC([this](uint8_t channel, uint8_t cc, uint8_t value) {
    handleCC(channel, cc, value);
});

// React to incoming notes
api.onNoteOn([this](uint8_t channel, uint8_t note, uint8_t velocity) {
    handleNoteOn(channel, note, velocity);
});

api.onNoteOff([this](uint8_t channel, uint8_t note, uint8_t velocity) {
    handleNoteOff(channel, note, velocity);
});

// React to incoming SysEx
api.onSysEx([this](const uint8_t* data, uint16_t length) {
    // Filter by manufacturer ID, parse protocol
    if (isMyProtocol(data, length)) {
        parseMessage(data, length);
    }
});
```

### Encoder Control

```cpp
// Sync encoder position with DAW value
api.setEncoderPosition(EncoderID::MACRO_1, 0.5f);  // Set to 50%

// Configure for discrete steps (e.g., 4-state button)
api.setEncoderDiscreteSteps(EncoderID::MACRO_1, 4);
// Will only emit at 0.0, 0.33, 0.67, 1.0

// Back to continuous
api.setEncoderContinuous(EncoderID::MACRO_1);

// Change mode dynamically
api.setEncoderMode(EncoderID::NAV, Hardware::EncoderMode::Relative);

// Set bounds for Relative mode
api.setEncoderBounds(EncoderID::NAV, 0.0f, 100.0f);

// Set delta per detent for Relative mode
api.setEncoderDelta(EncoderID::NAV, 1.0f);  // ±1 per detent
```

### View Management

```cpp
// Get plugin screen for creating UI
lv_obj_t* parent = api.getParentContainer();

// Show your view (switches to plugin screen)
api.showPluginView(myView);

// Hide and return to core screen
api.hidePluginView();
```

### Logging

```cpp
api.log("Simple message");
api.logf("Value: %d, Name: %s", value, name);
```

> Logging is only active in debug builds (`-DDEBUG_LOGS`).

---

## Creating Views

### IView Interface

```cpp
class IView {
public:
    virtual ~IView() = default;
    virtual void onActivate() = 0;    // Called when view becomes visible
    virtual void onDeactivate() = 0;  // Called when view is hidden
    virtual const char* getViewId() const = 0;
};
```

### View Implementation

```cpp
// plugin/myplugin/view/MainView.hpp
#pragma once

#include "ui/shared/interface/IView.hpp"
#include "api/ControllerAPI.hpp"
#include <lvgl.h>

namespace Plugin::MyPlugin {

class MainView : public IView
{
public:
    MainView(lv_obj_t* parent, ControllerAPI& api);
    ~MainView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "myplugin_main"; }

    // Custom methods
    void update();

private:
    void createUI();
    void setupBindings();

    ControllerAPI& api_;
    lv_obj_t* container_ = nullptr;

    // Widgets
    std::array<std::unique_ptr<ParameterKnobWidget>, 8> knobs_;
};

} // namespace Plugin::MyPlugin
```

### Using Core Widgets

Core provides reusable widgets:

```cpp
#include "ui/shared/widget/ParameterKnobWidget.hpp"
#include "ui/shared/widget/ListOverlay.hpp"
#include "ui/shared/widget/HintBar.hpp"
#include "ui/shared/widget/Label.hpp"

// Create a knob widget
auto knob = std::make_unique<ParameterKnobWidget>(
    parent,           // LVGL parent
    60,               // width
    80,               // height
    0,                // color index (0-7 for macro colors)
    false             // centered origin (bipolar)
);

knob->setName("Cutoff");
knob->setValue(0.5f);  // 0.0 - 1.0
```

### Using Theme Colors

```cpp
#include "ui/shared/theme/BaseTheme.hpp"

// Macro colors (for parameter widgets)
uint32_t color = BaseTheme::Color::getMacroColor(0);  // Red
uint32_t color = BaseTheme::Color::MACROS[3];         // Green

// UI colors
lv_obj_set_style_bg_color(obj, lv_color_hex(BaseTheme::Color::BACKGROUND), 0);
lv_obj_set_style_text_color(obj, lv_color_hex(BaseTheme::Color::TEXT_PRIMARY), 0);

// Layout constants
int margin = BaseTheme::Layout::MARGIN_MD;  // 8px
```

---

## Input IDs

### ButtonID

```cpp
// Navigation (left side)
ButtonID::LEFT_TOP      // 10
ButtonID::LEFT_CENTER   // 11
ButtonID::LEFT_BOTTOM   // 12

// Navigation (bottom)
ButtonID::BOTTOM_LEFT   // 20
ButtonID::BOTTOM_CENTER // 21
ButtonID::BOTTOM_RIGHT  // 22

// Macro buttons (encoder press)
ButtonID::MACRO_1 ... ButtonID::MACRO_8  // 31-38

// Special
ButtonID::NAV  // 40 (navigation encoder press)
```

### EncoderID

```cpp
// Macro encoders (8x)
EncoderID::MACRO_1 ... EncoderID::MACRO_8  // 301-308

// Special encoders
EncoderID::NAV  // 400 (Relative mode, infinite)
EncoderID::OPT  // 410 (high resolution)
```

---

## Best Practices

### Memory Management

```cpp
// Use unique_ptr for owned objects
std::unique_ptr<MainView> main_view_;

// Cleanup in destructor
~Plugin() {
    // Views auto-cleanup via unique_ptr
}

// Clear scoped bindings when view destroyed
~MainView() {
    api_.clearScope(container_);
    if (container_) {
        lv_obj_delete(container_);
    }
}
```

### State Synchronization

```cpp
// When receiving parameter update from DAW
void onParameterChanged(uint8_t index, float value) {
    // Update encoder position to match
    api_.setEncoderPosition(encoderForParam(index), value);

    // Update UI
    knobs_[index]->setValue(value);
}
```

### Error Handling

```cpp
bool Plugin::initialize() {
    if (!createView()) {
        api_.log("Failed to create view");
        return false;
    }

    if (!setupMidiHandlers()) {
        api_.log("Failed to setup MIDI");
        return false;
    }

    return true;
}
```

---

## Example: Parameter Page

```cpp
void ParameterPage::setupBindings() {
    // Each macro encoder controls its parameter
    for (uint8_t i = 0; i < 8; i++) {
        EncoderID enc = static_cast<EncoderID>(301 + i);

        api_.onTurned(enc, [this, i](float value) {
            // Send CC to DAW
            api_.sendCC(0, ccForParam(i), static_cast<uint8_t>(value * 127));

            // Update UI
            knobs_[i]->setValue(value);
        }, container_);  // Scoped to this view
    }

    // Navigation encoder scrolls pages
    api_.onTurned(EncoderID::NAV, [this](float delta) {
        if (delta > 0) nextPage();
        else prevPage();
    }, container_);
}
```

---

## Debugging

### Enable Debug Logs

In your `platformio.ini`:

```ini
build_flags =
    ${env.build_flags}
    -DDEBUG_LOGS
```

### Serial Monitor

```bash
pio device monitor
```

### Common Issues

| Issue | Solution |
|-------|----------|
| Bindings not firing | Check scope visibility, verify ButtonID/EncoderID |
| UI not updating | Call `api_.showPluginView()` after creating view |
| MIDI not received | Verify USB connection, check `onCC`/`onSysEx` callbacks |
| Memory issues | Use `unique_ptr`, avoid allocations in `update()` |

---

## See Also

- [Architecture](ARCHITECTURE.md) — Core architecture and patterns
- [Code Style](CODE_STYLE.md) — Coding conventions
- [ControllerAPI](../src/api/ControllerAPI.hpp) — Full API reference
- [Plugin Bitwig](https://github.com/petitechose-midi-studio/plugin-bitwig) — Complete example
