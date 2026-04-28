# Code Style & Conventions

This document defines the code conventions for MIDI Studio Core.

> **Auto-formatting**: Use the [`.clang-format`](../.clang-format) file at the project root.
> Run `clang-format -i <file>` or configure your IDE to format on save.

---

## Naming

### General Rules

| Element | Convention | Example |
|---------|------------|---------|
| Classes | `PascalCase` | `StandaloneContext`, `SequencerRuntimeService` |
| Interfaces | `I` + `PascalCase` | `IEventBus`, `IStorage`, `IContext` |
| Structs (data) | `PascalCase` | `ButtonBinding`, `GpioPin` |
| Enums | `PascalCase` | `ButtonBindingType`, `EncoderMode` |
| Enum values | `SCREAMING_SNAKE_CASE` | `LONG_PRESS`, `TURN_WHILE_PRESSED` |
| Functions/Methods | `camelCase` | `onPressed()`, `getSubscriberCount()` |
| Private members | `snake_case_` (trailing `_`) | `event_bus_`, `boot_complete_` |
| Local variables | `snake_case` | `normalized_value`, `press_time` |
| Constants | `SCREAMING_SNAKE_CASE` | `MAX_ACTIVE_NOTES`, `REFRESH_RATE_HZ` |
| Namespaces | `lowercase` | `base_theme::color`, `system::display` |
| Type aliases | `PascalCase` | `EventCallback`, `MidiChannelValue` |
| Template params | `T` or `PascalCase` | `template<typename Callback>` |

### Files

| Type | Convention | Example |
|------|------------|---------|
| Headers | `PascalCase.hpp` | `CoreState.hpp`, `SequencerRuntimeService.hpp` |
| Sources | `PascalCase.cpp` | `StandaloneContext.cpp` |
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
// Classes, structs, functions: K&R (brace on same line)
class MyClass {
public:
    void method();
};

struct MyData {
    int value;
};

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
SequencerRuntimeService(StateRefs state,
                        oc::api::MidiAPI& midi,
                        oc::interface::IEventBus& eventBus);

// Initializer lists
SequencerRuntimeService::SequencerRuntimeService(StateRefs state,
                                                 oc::api::MidiAPI& midi,
                                                 oc::interface::IEventBus& eventBus)
    : event_bus_(eventBus)
    , midi_(midi)
    , sequencer_state_(state.sequencer) {}

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
class MyClass {
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
namespace system::display {
    constexpr uint16_t SCREEN_WIDTH = 320;
    constexpr uint16_t SCREEN_HEIGHT = 240;
}

// Nested namespaces for logical organization
namespace base_theme {
namespace color {
    constexpr uint32_t BACKGROUND = 0x000000;
    constexpr uint32_t TEXT_PRIMARY = 0xFFFFFF;
}
namespace layout {
    constexpr int16_t MARGIN_SM = 4;
}
}
```

### Usage

```cpp
// Explicit usage (preferred)
auto width = system::display::SCREEN_WIDTH;
auto color = base_theme::color::BACKGROUND;

// No "using namespace" in headers
// ❌ using namespace std;
// ❌ using namespace base_theme;
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
        EventCategory::USER_INPUT,
        InputEvent::BUTTON_PRESS,
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

### Designated Initializers (Supported In Current Toolchain)

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

### VS Code Setup

This project uses **clangd** for IntelliSense and **clang-format** for automatic formatting.

#### Required Extensions

Install the recommended extensions (`.vscode/extensions.json`):

```json
{
    "recommendations": [
        "platformio.platformio-ide",
        "llvm-vs-code-extensions.vscode-clangd"
    ],
    "unwantedRecommendations": [
        "ms-vscode.cpptools-extension-pack"
    ]
}
```

> **Important**: Use clangd instead of Microsoft C/C++ IntelliSense for better performance and accuracy with embedded projects.

#### Workspace Settings

The project includes `.vscode/settings.json` with:

```json
{
    // Disable MS IntelliSense (use clangd)
    "C_Cpp.intelliSenseEngine": "disabled",

    // Clangd for Teensy toolchain
    "clangd.arguments": [
        "--query-driver=**/arm-none-eabi-*"
    ],

    // Format on save (modified lines only)
    "[cpp]": {
        "editor.defaultFormatter": "ms-vscode.cpptools",
        "editor.formatOnSave": true,
        "editor.formatOnSaveMode": "modifications"
    },

    // Use .clang-format file
    "C_Cpp.formatting": "clangFormat",
    "C_Cpp.clang_format_style": "file"
}
```

---

### clang-format

Automatic code formatting via `.clang-format` at project root.

#### Configuration

```yaml
BasedOnStyle: Google
Standard: c++17
IndentWidth: 4
ColumnLimit: 100

# Compact code
AllowShortBlocksOnASingleLine: Always
AllowShortFunctionsOnASingleLine: All
AllowShortIfStatementsOnASingleLine: AllIfsAndElse

# Include ordering (automatic)
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^"[^/]*\.hpp"'      # Paired header
    Priority: 1
  - Regex: '^<c(stdint|string)>' # C headers
    Priority: 2
  - Regex: '^<(vector|map)>'     # C++ STL
    Priority: 3
  - Regex: '^<'                  # External libs
    Priority: 4
  - Regex: '^"'                  # Project headers
    Priority: 5
```

#### Usage

```bash
# Format single file
clang-format -i src/path/to/File.cpp

# Format all source files
find src -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i
```

With VS Code, formatting happens automatically on save (modified lines only).

---

### clangd

Language server for IntelliSense, diagnostics, and navigation.

#### Configuration

The `.clangd` file configures clangd for the project:

```yaml
CompileFlags:
  CompilationDatabase: .

Diagnostics:
  Suppress:
    - ovl_diff_return_type  # GCC/Clang incompatibility (Teensy/Arduino)
```

#### Compilation Database

PlatformIO generates `compile_commands.json` automatically during build.

```bash
# Generate/update compilation database
pio run -e dev
```

> **Note**: Run a build after cloning to generate the compilation database. clangd won't work properly without it.

#### Troubleshooting

| Issue | Solution |
|-------|----------|
| Red squiggles everywhere | Run `pio run -e debug` to generate `compile_commands.json` |
| Arduino.h not found | Check `--query-driver` setting in clangd arguments |
| Wrong includes suggested | Restart clangd: `Ctrl+Shift+P` → "clangd: Restart" |

---

### Verification

```bash
# Build with warnings
pio run -e debug

# Format check (dry run)
clang-format --dry-run --Werror src/**/*.cpp

# Linting (if configured)
pio check
```
