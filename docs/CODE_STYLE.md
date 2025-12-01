# Code Style & Conventions

This document defines the code conventions for MIDI Studio Core.

---

## Naming

### General Rules

| Element | Convention | Example |
|---------|------------|---------|
| Classes | `PascalCase` | `MidiStudioApp`, `EncoderController` |
| Interfaces | `I` + `PascalCase` | `IEventBus`, `IView`, `IPlugin` |
| Structs (data) | `PascalCase` | `ButtonBinding`, `GpioPin` |
| Enums | `PascalCase` | `ButtonBindingType`, `EncoderMode` |
| Enum values | `SCREAMING_SNAKE_CASE` | `LONG_PRESS`, `TURN_WHILE_PRESSED` |
| Functions/Methods | `camelCase` | `onPressed()`, `getSubscriberCount()` |
| Private members | `snake_case_` (trailing `_`) | `event_bus_`, `boot_complete_` |
| Local variables | `snake_case` | `normalized_value`, `press_time` |
| Constants | `SCREAMING_SNAKE_CASE` | `MAX_ACTIVE_NOTES`, `REFRESH_RATE_HZ` |
| Namespaces | `PascalCase` | `BaseTheme::Color`, `System::Display` |
| Type aliases | `PascalCase` | `EventCallback`, `MidiChannelValue` |
| Template params | `T` or `PascalCase` | `template<typename Callback>` |

### Files

| Type | Convention | Example |
|------|------------|---------|
| Headers | `PascalCase.hpp` | `EventBus.hpp`, `MidiMapper.hpp` |
| Sources | `PascalCase.cpp` | `InputBinding.cpp` |
| One file = one concept | One main class/struct per file |

---

## Formatting

### Indentation

- **4 spaces** (no tabs)
- Configure editor to convert tabs → spaces

```cpp
void MyClass::doSomething() {
    if (condition) {
        processData();
    }
}
```

### Braces

```cpp
// Classes and structs: Allman (brace on new line)
class MyClass
{
public:
    void method();
};

struct MyData
{
    int value;
};

// Functions: K&R (brace on same line)
void shortFunction() {
    doWork();
}

// Conditions/loops: K&R
if (condition) {
    // ...
} else {
    // ...
}

for (const auto& item : items) {
    process(item);
}
```

### Line Length

- **Maximum ~100 characters**
- Break long declarations intelligently:

```cpp
// Parameters on multiple lines
explicit EncoderController(
    const std::vector<Hardware::Encoder>& encoderSetups,
    IEventBus& eventBus);

// Initializer lists
MidiStudioApp::MidiStudioApp(PluginSetupFn setupPlugins)
    : display_driver_(),
      setup_plugins_(setupPlugins),
      event_bus_(),
      multiplexer_() {}

// Chained calls
auto result = collection
    .filter(predicate)
    .transform(mapper)
    .collect();
```

### Spacing

```cpp
// Around operators
int result = a + b * c;
bool valid = (x > 0) && (y < 100);

// No space after function name
doSomething(arg1, arg2);

// Space after keywords
if (condition)
for (auto& item : items)
while (running)

// No space in templates
std::vector<int>
std::map<std::string, int>
```

---

## Includes

### Strict Order

Groups separated by blank line:

```cpp
#include "MyClass.hpp"           // 1. Paired header

#include <cstdint>               // 2. C standard headers
#include <cstring>

#include <vector>                // 3. C++ STL headers
#include <functional>
#include <optional>

#include <lvgl.h>                // 4. External libraries
#include <Arduino.h>

#include "core/event/Event.hpp"  // 5. Project headers
#include "config/System.hpp"
```

### Guards

Always use `#pragma once` (no traditional guards):

```cpp
#pragma once

// Header content...
```

---

## Classes

### Declaration Structure

```cpp
class MyClass
{
public:
    // 1. Types and aliases
    using Callback = std::function<void()>;

    // 2. Constructors / Destructor
    explicit MyClass(IEventBus& eventBus);
    ~MyClass();

    // 3. Rule of 5 (copy/move)
    MyClass(const MyClass&) = delete;
    MyClass& operator=(const MyClass&) = delete;
    MyClass(MyClass&&) = default;
    MyClass& operator=(MyClass&&) = default;

    // 4. Public methods
    void initialize();
    void update();
    size_t getCount() const;

private:
    // 5. Private methods
    void processInternal();

    // 6. Members (trailing underscore)
    IEventBus& event_bus_;
    bool initialized_ = false;
    std::vector<Item> items_;
};
```

### Constructors

```cpp
// Always explicit for single argument
explicit MyClass(int value);

// Prefer initializer list
MyClass::MyClass(IEventBus& bus, int count)
    : event_bus_(bus),
      count_(count),
      initialized_(false) {}
```

### Const Correctness

```cpp
// Const reference for input parameters
void process(const Event& event);
void setItems(const std::vector<Item>& items);

// Const methods when not mutating
size_t getCount() const;
bool isValid() const;
const std::string& getName() const;

// Constexpr for compile-time constants
static constexpr uint16_t BUFFER_SIZE = 1024;
```

---

## Enums

### Prefer `enum class`

```cpp
// Good: strongly-typed
enum class ButtonBindingType : uint8_t {
    PRESS,
    RELEASE,
    LONG_PRESS,
    DOUBLE_TAP,
    COMBO
};

// Usage requires explicit scope
ButtonBindingType type = ButtonBindingType::PRESS;

// Bad: classic enum (namespace pollution)
enum ButtonType { PRESS, RELEASE };  // ❌
```

### Explicit Values When Needed

```cpp
enum class ButtonID : uint16_t {
    LEFT_TOP = 10,
    LEFT_CENTER = 11,
    LEFT_BOTTOM = 12,

    MACRO_1 = 31,
    MACRO_2 = 32,
    // ...
};
```

---

## Namespaces

### Organization

```cpp
// Multi-level with ::
namespace System::Display {
    constexpr uint16_t SCREEN_WIDTH = 320;
    constexpr uint16_t SCREEN_HEIGHT = 240;
}

// Nested namespaces for logical organization
namespace BaseTheme {
namespace Color {
    constexpr uint32_t BACKGROUND = 0x000000;
    constexpr uint32_t TEXT_PRIMARY = 0xFFFFFF;
}
namespace Layout {
    constexpr int16_t MARGIN_SM = 4;
}
}
```

### Usage

```cpp
// Explicit usage (preferred)
auto width = System::Display::SCREEN_WIDTH;
auto color = BaseTheme::Color::BACKGROUND;

// No "using namespace" in headers
// ❌ using namespace std;
// ❌ using namespace BaseTheme;
```

---

## Documentation

### Public Headers (API)

```cpp
/**
 * @brief Register callback for button press event
 *
 * @param buttonId Button identifier
 * @param callback Action to execute on press
 *
 * @note Callback is invoked on main loop, not ISR context
 */
void onPressed(ButtonID buttonId, ActionCallback callback);
```

### Code Sections

```cpp
// ===== PUBLIC INTERFACE =====

void publicMethod1();
void publicMethod2();

// ===== INTERNAL HELPERS =====

void internalHelper();
```

### Inline Comments

```cpp
// Explain "why", not "what"
// ❌ Increment counter
// ✅ Skip first frame to allow LVGL layout calculation
count++;
```

---

## Embedded Specifics

### Memory

```cpp
// Prefer std::array for fixed sizes
std::array<uint8_t, 16> buffer;  // ✅
std::vector<uint8_t> buffer(16); // ❌ if size known

// Constexpr for constants
static constexpr size_t MAX_ITEMS = 32;

// Avoid allocations in hot paths
void update() {
    // ❌ std::string temp = "...";
    // ✅ const char* temp = "...";
}
```

### RAII

```cpp
// Automatic cleanup in destructors
~MyWidget() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}
```

### Smart Pointers

```cpp
// unique_ptr for exclusive ownership
std::vector<std::unique_ptr<Encoder>> encoders_;

// References for injected dependencies (no ownership)
IEventBus& event_bus_;  // ✅
std::shared_ptr<IEventBus> event_bus_;  // ❌ unnecessary overhead
```

### Conditional Logging

```cpp
// Use macros from log/Macros.hpp
LOGLN("[Module] Initialized");           // With newline
LOGF("[Module] Value: %d\n", value);     // Formatted
LOG("Partial ");                          // Without newline

// Compiled to no-op without DEBUG_LOGS
#ifdef DEBUG_LOGS
    // Debug-only code
#endif
```

---

## Common Patterns

### Event Subscription

```cpp
// In constructor
MyClass::MyClass(IEventBus& bus) : event_bus_(bus) {
    subscription_id_ = event_bus_.on(
        EventCategory::Input,
        InputEvent::ButtonPress,
        [this](const Event& e) { onButtonPress(e); }
    );
}

// In destructor
MyClass::~MyClass() {
    event_bus_.off(subscription_id_);
}
```

### Early Return

```cpp
// Prefer early return for readability
void process(const Data& data) {
    if (!data.isValid()) return;
    if (data.isEmpty()) return;

    // Main logic
    doWork(data);
}
```

### Designated Initializers (C++20)

```cpp
ButtonBinding binding{
    .type = ButtonBindingType::PRESS,
    .buttonId = ButtonID::MACRO_1,
    .action = std::move(callback),
    .enabled = true
};
```

---

## Anti-patterns to Avoid

### ❌ Don't Do

```cpp
using namespace std;              // Namespace pollution
#define MAX_SIZE 100             // Use constexpr
void foo(int x) { }              // Missing explicit if constructor
class foo { };                   // Wrong naming (lowercase)
int m_member;                    // m_ prefix (use _ suffix)
int member;                      // No suffix for private member
new Object();                    // Prefer smart pointers/RAII
```

### ✅ Do

```cpp
constexpr size_t MAX_SIZE = 100;
explicit Foo(int x);
class Foo { };
int member_;
std::make_unique<Object>();
```

---

## Tools

### Automatic Formatting

Recommended `.clang-format` configuration:

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
BreakBeforeBraces: Custom
BraceWrapping:
  AfterClass: true
  AfterStruct: true
  AfterFunction: false
```

### Verification

```bash
# Linting (if configured)
pio check

# Build with warnings
pio run -e debug
```
