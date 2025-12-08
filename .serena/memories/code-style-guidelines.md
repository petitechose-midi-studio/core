# Code Style & Architecture Guidelines - Open Control Framework

## Namespaces

```cpp
// Framework core
namespace oc::hal { }       // Hardware Abstraction Layer interfaces
namespace oc::core { }      // Business logic (EventBus, InputBinding)
namespace oc::context { }   // Context system (IContext, ContextManager)
namespace oc::api { }       // ButtonAPI, EncoderAPI, MidiAPI
namespace oc::app { }       // OpenControlApp, AppBuilder

// Platform-specific HAL implementations
namespace oc::teensy { }    // Teensy 4.x drivers (hal-teensy)
namespace oc::common { }    // Shared definitions (hal-common: EncoderDef, ButtonDef)

// UI layer
namespace oc::ui::lvgl { }  // LVGL integration (ui-lvgl)
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      main.cpp                               │  Entry Point
├─────────────────────────────────────────────────────────────┤
│                    OpenControlApp                           │  Composition Root
├─────────────────────────────────────────────────────────────┤
│     ContextManager + APIs (Button/Encoder/Midi)             │  Orchestration
├─────────────────────────────────────────────────────────────┤
│     Core (EventBus, InputBinding, Builders)                 │  Business Logic
├─────────────────────────────────────────────────────────────┤
│           HAL Interfaces (I*Driver, I*Controller)           │  Abstractions
├─────────────────────────────────────────────────────────────┤
│           Platform HAL (oc::teensy, oc::common)             │  Implementations
├─────────────────────────────────────────────────────────────┤
│                 UI (oc::ui::lvgl - optional)                │  Presentation
└─────────────────────────────────────────────────────────────┘
```

---

## Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes | `PascalCase` | `OpenControlApp`, `EncoderController` |
| Interfaces | `I` + `PascalCase` | `IEventBus`, `IView`, `IContext` |
| Enums | `PascalCase` | `ButtonBindingType`, `EncoderMode` |
| Enum values | `SCREAMING_SNAKE_CASE` | `LONG_PRESS`, `PLAY`, `VOLUME` |
| Functions/Methods | `camelCase` | `onPressed()`, `getSubscriberCount()` |
| Private members | `snake_case_` (trailing `_`) | `event_bus_`, `frame_` |
| Local variables | `snake_case` | `normalized_value`, `press_time` |
| Constants | `SCREAMING_SNAKE_CASE` | `MAX_ACTIVE_NOTES`, `APP_HZ` |
| Type aliases (IDs) | `PascalCase` with caps `ID` | `ButtonID`, `EncoderID` |

---

## Key Patterns

- **Dependency Injection**: Dependencies injected via constructor
- **Event Bus**: Decoupled pub/sub via typed events
- **RAII**: Automatic cleanup in destructors
- **Scoped Bindings**: Context-local bindings with priority (Scoped > Global)
- **Builder Pattern**: Fluent application construction

---

## Documentation

- **All comments MUST be in English**
- Use Doxygen for public API (`@brief`, `@param`, `@note`)
- Use `///` for inline enum/struct member docs
- Comment WHY, not WHAT
