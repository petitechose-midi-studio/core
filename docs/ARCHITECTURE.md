# Architecture

This document describes the architecture of MIDI Studio Core, its patterns and design principles.

---

## Overview

Core follows a **Clean Architecture** adapted for embedded systems, with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────┐
│                      main.cpp                               │  Entry Point
├─────────────────────────────────────────────────────────────┤
│                    MidiStudioApp                            │  Composition Root
├─────────────────────────────────────────────────────────────┤
│          Managers (Boot, View, Input, Plugin)               │  Orchestration
├─────────────────────────────────────────────────────────────┤
│     Core (EventBus, InputBinding, MidiMapper, Types)        │  Business Logic
├─────────────────────────────────────────────────────────────┤
│      Adapters (Display, Input, MIDI, Multiplexer)           │  HAL
├─────────────────────────────────────────────────────────────┤
│              UI (Views, Widgets, Theme)                     │  Presentation
├─────────────────────────────────────────────────────────────┤
│                   Config (constexpr)                        │  Configuration
└─────────────────────────────────────────────────────────────┘
```

---

## Layers

### 1. Entry Point (`main.cpp`)

Minimal Arduino entry point. Instantiates `MidiStudioApp` and delegates all work.

```cpp
MidiStudioApp app(setupPlugins);

void setup() { app.setup(); }
void loop() { app.update(); }
```

### 2. Composition Root (`app/MidiStudioApp`)

**Responsibility**: Assemble all dependencies and orchestrate lifecycle.

- Instantiates all components
- Injects dependencies via constructors
- Manages boot → main loop sequence
- Single point of configuration

```cpp
class MidiStudioApp {
    // All components instantiated here
    EventBus event_bus_;
    EncoderController encoders_;
    ButtonController buttons_;
    MidiMapper midi_mapper_;
    ViewManager ui_;
    // ...
};
```

### 3. Managers (`manager/`)

**Responsibility**: Orchestrate specific subsystems.

| Manager | Role |
|---------|------|
| `BootManager` | Phased initialization sequence |
| `ViewManager` | LVGL screen management (core/plugin) |
| `InputManager` | Encoder + button coordination |
| `PluginManager` | Plugin registration and lifecycle |

### 4. Core (`core/`)

**Responsibility**: Hardware-independent business logic.

| Component | Role |
|-----------|------|
| `EventBus` | Pub/Sub for decoupled communication |
| `InputBinding` | Input → action mapping (gestures) |
| `MidiMapper` | Input → MIDI message mapping |
| `Types` | Shared enums, structs, aliases |

### 5. Adapters (`adapter/`)

**Responsibility**: Hardware abstraction, Teensy-specific implementations.

| Adapter | Hardware |
|---------|----------|
| `Ili9341Driver` | SPI Display |
| `LVGLBridge` | LVGL ↔ Driver interface |
| `EncoderController` | Encoders via EncoderTool |
| `ButtonController` | Buttons via multiplexer |
| `TeensyUsbMidiIn/Out` | Native Teensy USB MIDI |
| `MultiplexerController` | CD74HC4067 |

### 6. UI (`ui/`)

**Responsibility**: LVGL visual components.

- `shared/widget/` — Reusable widgets (Knob, List, HintBar...)
- `shared/theme/` — Colors, layout, animations
- `shared/interface/` — `IView`, `IComponent`, `IWidget`
- `view/` — Concrete views (SplashScreen...)

### 7. Config (`config/`)

**Responsibility**: Compile-time configuration.

| File | Content |
|------|---------|
| `System.hpp` | Pins, timing, display dimensions |
| `InputDefinition.hpp` | Button/encoder hardware definitions |
| `InputID.hpp` | `ButtonID`, `EncoderID` enums |
| `Version.hpp` | Core and API versions |

---

## Patterns

### Event Bus (Pub/Sub)

Decoupled communication between components via typed events.

```cpp
// Publishing
event_bus_.emit(EncoderChangedEvent(encoderId, value));

// Subscribing
subscription_id_ = event_bus_.on(
    EventCategory::Input,
    InputEvent::EncoderChanged,
    [this](const Event& e) { handleEncoder(e); }
);

// Unsubscribing (in destructor)
event_bus_.off(subscription_id_);
```

**Event Categories**:
- `EventCategory::Input` — Buttons, encoders
- `EventCategory::Midi` — CC, Note, SysEx
- `EventCategory::System` — Boot, mode changes

**Benefits**:
- Complete emitter/receiver decoupling
- Extensible (new events without modifying existing code)
- Testable (mock EventBus)

### Dependency Injection

All dependencies are injected via constructor, never singletons.

```cpp
// ✅ Good: explicit injection
EncoderController(
    const std::vector<Hardware::Encoder>& config,
    IEventBus& eventBus
);

// ❌ Bad: hidden singleton
EncoderController() {
    eventBus_ = EventBus::getInstance();  // Anti-pattern
}
```

**Benefits**:
- Dependencies visible in signature
- Testable (mock injection)
- No hidden global state

### Factory Pattern

Centralized creation of hardware configurations.

```cpp
// core/factory/InputFactory.hpp
namespace InputFactory {
    std::vector<Hardware::Encoder> createEncoders();
    std::vector<Hardware::Button> createButtons();
}

// Usage in MidiStudioApp
encoders_config_(InputFactory::createEncoders())
```

### State Machine (Boot)

Boot sequence in distinct phases with visual progress.

```cpp
enum class Phase {
    NotStarted,
    HardwareInit,    // Multiplexer, display, encoders
    DisplayInit,     // LVGL init
    MinimalUI,       // Splash screen
    LoadingFonts,    // Progressive font loading
    InputInit,       // Flush encoder events
    MidiInit,        // USB MIDI
    Ready            // → Main loop
};
```

Each phase is atomic and boot progress can be visualized.

### Adapter Pattern

Hardware abstraction via interfaces.

```cpp
// Interface
class MidiOutput {
public:
    virtual void sendControlChange(uint8_t ch, uint8_t cc, uint8_t val) = 0;
    virtual void sendNoteOn(uint8_t ch, uint8_t note, uint8_t vel) = 0;
    // ...
};

// Teensy implementation
class TeensyUsbMidiOut : public MidiOutput {
    void sendControlChange(...) override {
        usbMIDI.sendControlChange(...);
    }
};
```

**Benefits**:
- Business code independent of hardware
- Portability (new MCU = new adapter)
- Testability (mock adapters)

### Scoped Bindings

Context-aware input bindings tied to LVGL visibility.

```cpp
// Binding active only if `overlay` is visible
api.onTurned(EncoderID::NAV, [this](float v) {
    scrollList(v);
}, overlay_container_);

// Global binding (always active)
api.onPressed(ButtonID::BOTTOM_LEFT, [this]() {
    goBack();
});
```

**Priority**: Scoped > Global (propagation stops if scoped triggered)

---

## Data Flow

### Input → Action

```
Hardware Event (ISR)
       ↓
EncoderController / ButtonController
       ↓
EventBus.emit(InputEvent)
       ↓
   ┌───┴───┐
   ↓       ↓
MidiMapper  InputBinding
   ↓           ↓
MIDI Out    Plugin callbacks
```

### MIDI In → Plugin

```
USB MIDI (ISR buffer)
       ↓
TeensyUsbMidiIn.processPendingMessages()
       ↓
EventBus.emit(MidiEvent)
       ↓
Plugin.onCC() / onSysEx()
```

---

## Compile-Time Configuration

All hardware constants are `constexpr` for zero runtime overhead.

```cpp
// config/System.hpp
namespace System::Display {
    constexpr uint16_t SCREEN_WIDTH = 320;
    constexpr uint16_t SCREEN_HEIGHT = 240;
    constexpr int REFRESH_RATE_HZ = 60;
}

namespace System::Hardware {
    constexpr uint8_t DISPLAY_CS_PIN = 28;
    constexpr uint8_t DISPLAY_DC_PIN = 0;
    // ...
}
```

```cpp
// config/InputDefinition.hpp
constexpr Hardware::Encoder ENCODERS[] = {
    {EncoderID::MACRO_1, mcuPin(22), mcuPin(23)},
    {EncoderID::MACRO_2, mcuPin(18), mcuPin(19)},
    // ...
};
```

---

## Plugin System

### IPlugin Interface

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual bool initialize() = 0;  // Setup after registration
    virtual void cleanup() = 0;     // Teardown
    virtual void update() = 0;      // Called each frame

    virtual const char* getName() const = 0;
    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
};
```

### ControllerAPI

Unified interface for plugins to interact with Core:

- **Input Binding**: `onPressed()`, `onTurned()`, gestures...
- **MIDI**: `sendCC()`, `sendSysEx()`, `onCC()`...
- **Encoder Control**: `setEncoderPosition()`, `setEncoderMode()`...
- **View Management**: `getParentContainer()`, `showPluginView()`...

→ See [src/api/ControllerAPI.hpp](../src/api/ControllerAPI.hpp)

### Registration

```cpp
void setupPlugins(PluginManager& manager) {
    manager.registerPlugin<BitwigPlugin>("bitwig");
}

MidiStudioApp app(setupPlugins);
```

---

## Versioning

### Semantic Versioning

**Core Version** (`MAJOR.MINOR.PATCH[-PRERELEASE]`)
- `MAJOR`: Breaking structural changes
- `MINOR`: New features (backward-compatible)
- `PATCH`: Bug fixes

**API Version** (`MAJOR.MINOR.PATCH`)
- Evolves independently from Core
- `MAJOR`: Breaking API changes (plugins must adapt)
- `MINOR`: New API methods (backward-compatible)

### Version.hpp

```cpp
namespace Core {
    constexpr uint8_t VERSION_MAJOR = 1;
    constexpr uint8_t VERSION_MINOR = 0;
    constexpr uint8_t VERSION_PATCH = 0;
    constexpr const char* VERSION = "1.0.0-beta.1";
}

namespace API {
    constexpr uint8_t VERSION_MAJOR = 1;
    constexpr uint8_t VERSION_MINOR = 0;
    constexpr uint8_t VERSION_PATCH = 0;
}
```

### Release Policy

```ini
# Production: always use specific tag
lib_deps = https://github.com/.../core.git#v1.0.0

# ❌ Never use in production
lib_deps = https://github.com/.../core.git#main
lib_deps = file://../core
```

---

## Performance

### Present Optimizations

| Technique | Location | Effect |
|-----------|----------|--------|
| `constexpr` config | `System.hpp`, `InputDefinition.hpp` | Zero runtime cost |
| Diff buffer | Display (16KB) | Reduces SPI transfers |
| Batch event flush | `Encoder::flushEvents()` | Prevents ISR flood |
| Lightweight ISR callbacks | EncoderTool | Minimal ISR time |
| `std::optional` | `BootManager`, views | Lazy initialization |

### Guidelines

- Avoid dynamic allocations in `update()`
- Prefer `std::array` over `std::vector` for known sizes
- Use `constexpr` for all constants
- Systematic RAII (cleanup in destructors)

---

## Testability

The architecture facilitates testing via:

1. **Interfaces** (`IEventBus`, `IView`, `MidiOutput`...) → Mockable
2. **Dependency Injection** → No global state
3. **Layer separation** → Unit tests per component
4. **External configuration** → Tests with different configs

```cpp
// Test with mock EventBus
MockEventBus mockBus;
EncoderController controller(config, mockBus);

controller.simulateRotation(EncoderID::MACRO_1, 10);

EXPECT_TRUE(mockBus.wasEmitted(InputEvent::EncoderChanged));
```

---

## Dependency Diagram

```
                    ┌──────────────┐
                    │   main.cpp   │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │MidiStudioApp │
                    └──────┬───────┘
                           │
         ┌─────────────────┼─────────────────┐
         │                 │                 │
    ┌────▼────┐      ┌─────▼─────┐     ┌─────▼─────┐
    │ Managers │      │   Core    │     │  Adapters │
    └────┬────┘      └─────┬─────┘     └─────┬─────┘
         │                 │                 │
         │           ┌─────▼─────┐           │
         └──────────►│ EventBus  │◄──────────┘
                     └─────┬─────┘
                           │
                     ┌─────▼─────┐
                     │  Plugins  │
                     └───────────┘
```

**Key Rule**: Dependencies point toward the center (EventBus, Core).
Outer layers don't know each other directly.
