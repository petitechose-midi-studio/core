# State Management Guide

> **Type**: Tutorial
> **Audience**: Developers adding features to MIDI Studio
> **Prerequisites**: Basic C++17, understanding of reactive programming concepts

This guide explains how to use the Signal-based reactive state system in MIDI Studio.

---

## Table of Contents

1. [Core Concepts](#1-core-concepts)
2. [Signal Types](#2-signal-types)
3. [Creating State Structs](#3-creating-state-structs)
4. [Subscribing to State Changes](#4-subscribing-to-state-changes)
5. [Modifying State (Handlers)](#5-modifying-state-handlers)
6. [Best Practices](#6-best-practices)
7. [Complete Example](#7-complete-example)

---

## 1. Core Concepts

### The Reactive Flow

```
Handler (Input/Host)          State (Signals)              View (UI)
       │                           │                          │
       │  state_.value.set(x)      │                          │
       ├──────────────────────────>│                          │
       │                           │  callback fires          │
       │                           ├─────────────────────────>│
       │                           │                  lv_*()  │
```

**Key principles:**

| Principle | Description |
|-----------|-------------|
| **Single Source of Truth** | State structs hold the canonical data |
| **Unidirectional Flow** | Handlers → State → Views (never reversed) |
| **Automatic Updates** | Views subscribe and react to changes |
| **Decoupled Components** | Handlers don't know about Views |

### Why Signals?

```cpp
// WITHOUT Signals (imperative, tightly coupled)
void onEncoderTurn(float value) {
    value_ = value;
    knobWidget_->setValue(value);      // Handler knows about UI!
    label_->setText(formatValue());    // Coupling everywhere
    saveToStorage();
}

// WITH Signals (reactive, decoupled)
void onEncoderTurn(float value) {
    state_.macro.value.set(value);     // Handler only knows State
    // Views update themselves automatically via subscriptions
}
```

---

## 2. Signal Types

### Available Types

| Type | Header | Use Case | Example |
|------|--------|----------|---------|
| `Signal<T>` | `<oc/state/Signal.hpp>` | Primitives, small structs | `Signal<float>`, `Signal<bool>` |
| `SignalLabel` | `<oc/state/SignalString.hpp>` | Short strings (32 chars) | Track names, parameter labels |
| `SignalTiny` | `<oc/state/SignalString.hpp>` | Very short strings (8 chars) | CC values ("127"), indices |
| `SignalVector<T,N>` | `<oc/state/SignalVector.hpp>` | Fixed-size collections | Parameter lists |

### Basic Usage

```cpp
#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

// Declare signals
Signal<float> value{0.5f};           // With default
Signal<bool> active;                 // Default: false
SignalLabel name;                    // Default: empty string
SignalTiny displayValue;             // Default: empty string

// Read values
float v = value.get();
const char* s = name.get();

// Write values (triggers subscribers)
value.set(0.75f);
name.set("Macro 1");
```

### Change Detection

Signals only notify subscribers when the value actually changes:

```cpp
Signal<int> counter{10};

counter.set(10);  // No notification (same value)
counter.set(11);  // Notification triggered
counter.set(11);  // No notification (same value)
```

---

## 3. Creating State Structs

### Pattern: State Slot

A "slot" groups related signals for a single entity (parameter, track, macro, etc.):

```cpp
// File: state/MacroState.hpp

#pragma once

#include <oc/state/Signal.hpp>
#include <oc/state/SignalString.hpp>

namespace core::state {

using oc::state::Signal;
using oc::state::SignalLabel;
using oc::state::SignalTiny;

/**
 * @brief Single macro slot state
 */
struct MacroSlot {
    Signal<float> value{0.5f};     ///< Normalized value [0.0, 1.0]
    SignalLabel label;              ///< Display label ("Macro 1")
    SignalTiny displayValue;        ///< CC value as string ("64")

    /// Derived value helper
    void updateDisplayValue() {
        uint8_t cc = static_cast<uint8_t>(value.get() * 127.0f);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", cc);
        displayValue.set(buf);
    }

    /// Reset to default
    void reset() {
        value.set(0.5f);
        updateDisplayValue();
    }
};

}  // namespace core::state
```

### Pattern: State Container

A container groups multiple slots and provides access:

```cpp
// Still in state/MacroState.hpp

static constexpr uint8_t MACRO_COUNT = 8;

/**
 * @brief Reactive state for 8 macro parameters
 */
struct MacroState {
    MacroSlot slots[MACRO_COUNT];

    /// Initialize with default labels
    MacroState() {
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            char buf[16];
            snprintf(buf, sizeof(buf), "Macro %d", i + 1);
            slots[i].label.set(buf);
            slots[i].updateDisplayValue();
        }
    }

    /// Indexed access
    MacroSlot& operator[](uint8_t index) { return slots[index]; }
    const MacroSlot& operator[](uint8_t index) const { return slots[index]; }
};
```

### Pattern: Aggregated State

The top-level state aggregates all domain states:

```cpp
// File: state/CoreState.hpp

#pragma once

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/MacroSettings.hpp"

namespace core::state {

/**
 * @brief Complete application state
 */
struct CoreState {
    MacroState macros;
    StatusBarState statusBar;
    MacroSettings macroSettings;

    // Convenience methods for common operations
    void setMacroValue(uint8_t index, float value) {
        macros[index].value.set(value);
        macros[index].updateDisplayValue();
    }

    const MacroConfig& getMacroConfig(uint8_t index) const {
        return macroSettings.configs[index];
    }
};

}  // namespace core::state
```

---

## 4. Subscribing to State Changes

### Basic Subscription

```cpp
#include <oc/state/Signal.hpp>

class MyView {
public:
    MyView(CoreState& state) : state_(state) {
        // Subscribe to value changes
        sub_ = state_.macros[0].value.subscribe([this](float value) {
            // This callback runs when value changes
            knob_.setValue(value);
        });
    }

private:
    CoreState& state_;
    oc::state::Subscription sub_;  // RAII: auto-unsubscribes on destruction
    KnobWidget knob_;
};
```

### Managing Multiple Subscriptions

```cpp
class MacroView {
public:
    MacroView(CoreState& state) : state_(state) {
        subscriptions_.reserve(MACRO_COUNT);

        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            // Capture index by value, not reference
            subscriptions_.push_back(
                state_.macros[i].value.subscribe([this, i](float value) {
                    widgets_[i]->setValue(value);
                })
            );
        }
    }

private:
    std::vector<oc::state::Subscription> subscriptions_;
};
```

### Debounced Updates (Recommended for UI)

For high-frequency updates (encoders, sliders), debounce to avoid redraw storms:

```cpp
class MacroView {
public:
    MacroView(CoreState& state) : state_(state) {
        // Timer synced with display refresh rate
        constexpr uint32_t periodMs = 1000 / 60;  // 60 Hz
        timer_ = lv_timer_create(onTimer, periodMs, this);

        // Subscribe sets dirty flag instead of immediate update
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            subs_.push_back(state_.macros[i].value.subscribe(
                [this, i](float) { dirty_[i] = true; }
            ));
        }
    }

    ~MacroView() {
        if (timer_) lv_timer_delete(timer_);
    }

private:
    static void onTimer(lv_timer_t* t) {
        auto* self = static_cast<MacroView*>(lv_timer_get_user_data(t));
        self->processDirty();
    }

    void processDirty() {
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            if (dirty_[i]) {
                dirty_[i] = false;
                widgets_[i]->setValue(state_.macros[i].value.get());
            }
        }
    }

    std::array<bool, MACRO_COUNT> dirty_{};
    lv_timer_t* timer_ = nullptr;
};
```

---

## 5. Modifying State (Handlers)

### InputHandler Pattern

Handlers translate user input into state changes:

```cpp
// File: handler/input/HandlerInputMacro.cpp

#include "HandlerInputMacro.hpp"
#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

HandlerInputMacro::HandlerInputMacro(
    CoreState& state,
    EncoderAPI& encoders,
    MidiAPI& midi,
    lv_obj_t* scopeElement)
    : state_(state)
    , encoders_(encoders)
    , midi_(midi)
    , scope_element_(scopeElement)
{
    setupBindings();
}

void HandlerInputMacro::setupBindings() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope(scope_element_))  // Scoped binding
            .then([this, i](float value) {
                handleValueChange(i, value);
            });
    }
}

void HandlerInputMacro::handleValueChange(uint8_t index, float value) {
    // 1. Update state (triggers UI via subscriptions)
    state_.setMacroValue(index, value);

    // 2. Send MIDI (external action)
    const auto& config = state_.getMacroConfig(index);
    uint8_t cc = static_cast<uint8_t>(value * 127.0f);
    midi_.sendCC(config.channel, config.cc, cc);

    // 3. Update activity indicator
    state_.statusBar.ccOutActive.set(true);
}

}  // namespace core::handler
```

### What Handlers Can Do

| Action | Allowed | Example |
|--------|---------|---------|
| `state_.*.set()` | ✅ Yes | `state_.macros[i].value.set(0.5f)` |
| `protocol_.send()` | ✅ Yes | `protocol_.send(MacroValueMessage{...})` |
| `midi_.sendCC()` | ✅ Yes | `midi_.sendCC(1, 74, 64)` |
| `lv_*()` (LVGL) | ❌ **Never** | Violates invariant |
| View methods | ❌ **Never** | Coupling violation |

---

## 6. Best Practices

### DO ✅

```cpp
// Capture loop variable by value
for (uint8_t i = 0; i < COUNT; ++i) {
    subs_.push_back(signal.subscribe([this, i](float v) { ... }));
    //                                       ^^^ by value
}

// Store subscriptions in class member
std::vector<Subscription> subs_;  // Lives as long as the class

// Use helper methods for compound updates
void CoreState::setMacroValue(uint8_t i, float v) {
    macros[i].value.set(v);
    macros[i].updateDisplayValue();  // Keeps display in sync
}

// Clear subscriptions in destructor
~MyView() {
    subscriptions_.clear();
}
```

### DON'T ❌

```cpp
// Capture by reference in loop (WRONG - undefined behavior)
for (uint8_t i = 0; i < COUNT; ++i) {
    subs_.push_back(signal.subscribe([this, &i](float v) { ... }));
    //                                       ^^^ WRONG!
}

// Forget to store subscription (callback never fires)
void MyView::init() {
    state_.value.subscribe([](float v) { ... });  // WRONG: subscription dies
}

// Call lv_* in handler (WRONG - violates invariant)
void HandlerInput::onTurn(float v) {
    state_.value.set(v);
    lv_label_set_text(label_, "...");  // WRONG!
}

// Use notifyImmediate() without reason
state_.value.notifyImmediate();  // Bypasses coalescing - avoid
```

---

## 7. Complete Example

### Adding a New "Volume" State

**Step 1: Create the state struct**

```cpp
// File: state/VolumeState.hpp
#pragma once

#include <oc/state/Signal.hpp>

namespace core::state {

using oc::state::Signal;

struct VolumeState {
    Signal<float> level{0.75f};    ///< 0.0 to 1.0
    Signal<bool> muted{false};

    void toggleMute() { muted.set(!muted.get()); }
};

}  // namespace core::state
```

**Step 2: Add to CoreState**

```cpp
// In state/CoreState.hpp
#include "state/VolumeState.hpp"

struct CoreState {
    MacroState macros;
    VolumeState volume;  // Add here
    // ...
};
```

**Step 3: Create handler**

```cpp
// File: handler/input/HandlerInputVolume.cpp
void HandlerInputVolume::setupBindings() {
    buttons_.button(ButtonID::MUTE)
        .press()
        .scope(scope(scope_element_))
        .then([this]() { state_.volume.toggleMute(); });

    encoders_.encoder(EncoderID::MAIN)
        .turn()
        .scope(scope(scope_element_))
        .then([this](float delta) {
            float newLevel = std::clamp(state_.volume.level.get() + delta, 0.f, 1.f);
            state_.volume.level.set(newLevel);
        });
}
```

**Step 4: Subscribe in View**

```cpp
// In ui/view/VolumeView.cpp
void VolumeView::bindToState() {
    subs_.push_back(
        state_.volume.level.subscribe([this](float level) {
            slider_.setValue(level);
        })
    );

    subs_.push_back(
        state_.volume.muted.subscribe([this](bool muted) {
            muteButton_.setActive(muted);
        })
    );
}
```

---

## Summary Cheatsheet

| Task | Code |
|------|------|
| Create signal | `Signal<float> value{0.5f};` |
| Read value | `float v = value.get();` |
| Write value | `value.set(0.75f);` |
| Subscribe | `sub_ = value.subscribe([](float v) { ... });` |
| Store subs | `std::vector<Subscription> subs_;` |
| Debounce | Use dirty flags + timer |

---

## See Also

- [INVARIANTS.md](INVARIANTS.md) - Rules about handler/view boundaries
- [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md) - Input binding patterns
- [HOW_TO_ADD_VIEW.md](HOW_TO_ADD_VIEW.md) - View lifecycle and subscriptions
