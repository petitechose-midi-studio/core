# Core

Hardware-independent business logic for MIDI Studio.

## Overview

This directory contains the core systems that are independent of specific hardware implementations:

- **Event Bus** — Pub/Sub communication system
- **Input Binding** — Input-to-action mapping with gestures
- **MIDI Mapper** — Input-to-MIDI mapping
- **Types** — Shared enums, structs, and type aliases
- **Factories** — Hardware configuration creation

## Directory Structure

```
core/
├── event/          # Event system
│   ├── Event.hpp           # Base Event class
│   ├── Events.hpp          # Concrete event types
│   ├── EventBus.hpp        # Pub/Sub implementation
│   ├── IEventBus.hpp       # EventBus interface
│   └── UnifiedEventTypes.hpp  # Event categories and types
├── factory/        # Configuration factories
│   ├── InputFactory.hpp    # Creates encoder/button configs
│   └── MidiFactory.hpp     # Creates MIDI mappings
├── input/          # Input handling
│   ├── InputBinding.hpp    # Input-to-action binding
│   └── InputBinding.cpp
├── interface/      # Core interfaces
│   └── midi/
│       ├── MidiInput.hpp   # IMidiInput interface
│       └── MidiOutput.hpp  # IMidiOutput interface
├── midi/           # MIDI processing
│   ├── MidiMapper.hpp      # Input-to-MIDI mapping
│   └── MidiMapper.cpp
├── struct/         # Data structures
│   ├── Binding.hpp         # ButtonBinding, EncoderBinding
│   ├── Button.hpp          # Hardware::Button
│   ├── Encoder.hpp         # Hardware::Encoder
│   └── MidiCCMapping.hpp   # MIDI CC mapping struct
└── Type.hpp        # Core type definitions
```

---

## Event System

### Event Base Class

```cpp
class Event {
public:
    Event(EventCategoryType category, EventType type);
    EventCategoryType getCategory() const;
    EventType getType() const;
};
```

### Event Categories

```cpp
namespace EventCategory {
constexpr EventCategoryType SYSTEM = 0;
constexpr EventCategoryType USER_INPUT = 1;  // Button, encoder events
constexpr EventCategoryType MIDI = 2;        // CC, Note, SysEx events
constexpr EventCategoryType UI = 3;
constexpr EventCategoryType INTEGRATION = 4; // Plugin events
}
```

### Concrete Events

| Event | Category | Data |
|-------|----------|------|
| `EncoderChangedEvent` | USER_INPUT | encoderId, normalizedValue |
| `ButtonPressEvent` | USER_INPUT | buttonId |
| `ButtonReleaseEvent` | USER_INPUT | buttonId |
| `MidiCCEvent` | MIDI | channel, controller, value |
| `MidiNoteOnEvent` | MIDI | channel, note, velocity |
| `MidiNoteOffEvent` | MIDI | channel, note, velocity |
| `SysExEvent` | MIDI | data, length |
| `SystemBootCompleteEvent` | SYSTEM | — |
| `SystemModeChangedEvent` | SYSTEM | mode |

### EventBus

```cpp
class EventBus : public IEventBus {
public:
    // Subscribe to events
    SubscriptionId on(EventCategoryType category, EventType type, EventCallback callback);

    // Publish event
    void emit(const Event& event);

    // Unsubscribe
    void off(SubscriptionId id);

    // Utilities
    void clear();
    size_t getSubscriberCount() const;
};
```

**Usage:**

```cpp
// Subscribe
auto id = eventBus.on(EventCategory::USER_INPUT, InputEvent::BUTTON_PRESS,
    [this](const Event& e) {
        auto& evt = static_cast<const ButtonPressEvent&>(e);
        handleButton(evt.buttonId);
    });

// Publish
eventBus.emit(ButtonPressEvent(ButtonID::MACRO_1));

// Unsubscribe (typically in destructor)
eventBus.off(id);
```

---

## Input Binding

Maps hardware inputs to callbacks with gesture support.

### Supported Gestures

| Gesture | Description |
|---------|-------------|
| `PRESS` | Button down |
| `RELEASE` | Button up |
| `LONG_PRESS` | Held for duration (configurable) |
| `DOUBLE_TAP` | Two taps within window |
| `COMBO` | Two buttons pressed together |
| `TURN` | Encoder rotation |
| `TURN_WHILE_PRESSED` | Encoder + button held |

### Scoped Bindings

Bindings can be scoped to LVGL objects:

```cpp
// Global binding (always active)
binding.onPressed(ButtonID::MACRO_1, callback);

// Scoped binding (active only if object visible)
binding.onPressed(ButtonID::MACRO_1, callback, lvgl_object);

// With latch behavior
binding.onPressed(ButtonID::MACRO_1, callback, lvgl_object, true);
```

**Priority:** Scoped bindings take precedence. If a scoped binding handles an event, global bindings are not triggered.

### Binding Structs

```cpp
struct ButtonBinding {
    ButtonBindingType type;
    ButtonID buttonId;
    std::optional<ButtonID> secondaryButton;  // For COMBO
    uint32_t longPressMs = 0;
    std::function<void()> action;
    bool enabled = true;
    bool latch = false;
    lv_obj_t* scope = nullptr;  // nullptr = global
};

struct EncoderBinding {
    EncoderBindingType type;
    EncoderID encoderId;
    std::optional<ButtonID> requiredButton;  // For TURN_WHILE_PRESSED
    std::function<void(float)> action;
    bool enabled = true;
    lv_obj_t* scope = nullptr;
};
```

---

## MIDI Mapper

Maps hardware inputs directly to MIDI messages.

```cpp
class MidiMapper {
public:
    MidiMapper(IMidiOutput& midiOut, IEventBus& eventBus,
               const MidiConfig& config);

private:
    void onEncoderChangedEvent(const Event& e);
    void onButtonPressEvent(const Event& e);
};
```

Uses configuration from `MidiFactory::createDefault()`.

---

## Types (Type.hpp)

### Enums

```cpp
enum class ButtonBindingType : uint8_t {
    PRESS, RELEASE, LONG_PRESS, DOUBLE_TAP, COMBO
};

enum class EncoderBindingType : uint8_t {
    TURN, TURN_WHILE_PRESSED
};

enum class PinMode { PULLUP, PULLDOWN, RAW };
```

### GpioPin

```cpp
struct GpioPin {
    enum class Source { MCU, MUX };

    Source source = Source::MCU;
    uint8_t pin = 0;
    PinMode mode = PinMode::PULLUP;

    constexpr bool isMultiplexed() const;
    constexpr uint8_t getMuxChannel() const;
};

// Helpers
constexpr GpioPin mcuPin(uint8_t pin, PinMode mode = PinMode::PULLUP);
constexpr GpioPin muxPin(uint8_t channel, PinMode mode = PinMode::PULLUP);
```

### Type Aliases

```cpp
using MidiChannelValue = uint8_t;
using MidiCCValue = uint8_t;
using MidiNoteValue = uint8_t;
```

---

## Factories

### InputFactory

Creates hardware configurations from `InputDefinition.hpp`:

```cpp
namespace InputFactory {
    std::vector<Hardware::Encoder> createEncoders();
    std::vector<Hardware::Button> createButtons();
}
```

### MidiFactory

Creates default MIDI mappings:

```cpp
namespace MidiFactory {
    MidiMapper::MidiConfig createDefault();
}
```

---

## See Also

- [Architecture](../../docs/ARCHITECTURE.md) — EventBus pattern details
- [Config](../config/) — Hardware definitions
- [Adapters](../adapter/) — Hardware implementations
