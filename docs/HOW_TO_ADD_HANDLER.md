# How To Add a Handler

> **Type**: Step-by-step Tutorial
> **Audience**: Developers adding input handling to MIDI Studio
> **Time**: 30-45 minutes
> **Prerequisites**: [STATE_MANAGEMENT.md](STATE_MANAGEMENT.md)

This guide explains how to create InputHandlers that respond to buttons, encoders, and other input sources.

---

## Table of Contents

1. [Handler Architecture](#1-handler-architecture)
2. [Step 1: Define the Handler Class](#step-1-define-the-handler-class)
3. [Step 2: Setup Input Bindings](#step-2-setup-input-bindings)
4. [Step 3: Implement Action Methods](#step-3-implement-action-methods)
5. [Step 4: Register in Context](#step-4-register-in-context)
6. [Binding Patterns](#binding-patterns)
7. [Complete Example](#complete-example)

---

## 1. Handler Architecture

### Handler Types

| Type | Direction | Purpose | Example |
|------|-----------|---------|---------|
| **InputHandler** | User → State | Translates user input to state changes | `MacroHandler` |
| **HostHandler** | Protocol → State | Translates host messages to state | `DeviceHostHandler` |

This guide focuses on **InputHandlers**.

### Data Flow

```
Button/Encoder      Handler           State            View
      │                │                │                │
      │  callback      │                │                │
      ├───────────────>│                │                │
      │                │  state.set()   │                │
      │                ├───────────────>│                │
      │                │                │   subscribe    │
      │                │                ├───────────────>│
      │                │                │       lv_*()   │
```

### What Handlers Can/Cannot Do

| Action | ✅ Allowed | ❌ Forbidden |
|--------|----------|-----------|
| `state_.*.set()` | Yes | - |
| `protocol_.send()` | Yes | - |
| `midi_.sendCC()` | Yes | - |
| `lv_*()` (LVGL calls) | - | Violates invariant |
| Direct view access | - | Coupling violation |

---

## Step 1: Define the Handler Class

```cpp
// File: src/handler/volume/VolumeHandler.hpp
#pragma once

/**
 * @file VolumeHandler.hpp
 * @brief Handles volume controls (level, mute)
 *
 * - MAIN encoder: volume +/-
 * - MUTE button: toggle mute
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"

namespace core::handler {

class VolumeHandler {
public:
    VolumeHandler(
        core::state::CoreState& coreState,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* scopeElement);

    ~VolumeHandler() = default;

    // Non-copyable
    VolumeHandler(const VolumeHandler&) = delete;
    VolumeHandler& operator=(const VolumeHandler&) = delete;

private:
    void setupBindings();
    void handleVolumeChange(float delta);
    void handleMuteToggle();

    core::state::CoreState& core_state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* scope_element_;

    static constexpr float VOLUME_MIN = 0.0f;
    static constexpr float VOLUME_MAX = 1.0f;
    static constexpr float VOLUME_STEP = 0.01f;  // 1% per tick
};

}  // namespace core::handler
```

**Key elements:**

- Constructor takes references to APIs and state
- `scopeElement` for scoped input bindings
- Private `setupBindings()` called in constructor
- Action methods are private

---

## Step 2: Setup Input Bindings

```cpp
// File: src/handler/volume/VolumeHandler.cpp
#include "VolumeHandler.hpp"

#include <algorithm>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

VolumeHandler::VolumeHandler(
    core::state::CoreState& coreState,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* scopeElement)
    : core_state_(coreState)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_element_(scopeElement)
{
    setupBindings();
}

void VolumeHandler::setupBindings() {
    // Encoder binding
    encoders_.encoder(Config::EncoderID::MAIN)
        .turn()
        .scope(scope(scope_element_))
        .then([this](float delta) { handleVolumeChange(delta); });

    // Button binding
    buttons_.button(Config::ButtonID::MUTE)
        .press()
        .scope(scope(scope_element_))
        .then([this]() { handleMuteToggle(); });
}

// ... action methods
}  // namespace core::handler
```

### Binding Fluent API

```cpp
// Encoder turn
encoders_.encoder(EncoderID)
    .turn()                          // On rotation
    .scope(scope(element))           // Scoped to UI element
    .then([](float delta) { });      // Callback with delta

// Button press
buttons_.button(ButtonID)
    .press()                         // On press down
    .scope(scope(element))
    .then([]() { });                 // Callback (no params)

// Button long press
buttons_.button(ButtonID)
    .longPress()
    .threshold(500)                  // Milliseconds
    .scope(scope(element))
    .then([]() { });

// Button release
buttons_.button(ButtonID)
    .release()
    .scope(scope(element))
    .then([]() { });
```

---

## Step 3: Implement Action Methods

```cpp
void VolumeHandler::handleVolumeChange(float delta) {
    // 1. Read current state
    float current = core_state_.volume.level.get();

    // 2. Calculate new value
    float newLevel = current + (delta * VOLUME_STEP);
    newLevel = std::clamp(newLevel, VOLUME_MIN, VOLUME_MAX);

    // 3. Update state (triggers UI via subscriptions)
    core_state_.volume.level.set(newLevel);
}

void VolumeHandler::handleMuteToggle() {
    // Toggle state
    bool muted = core_state_.volume.muted.get();
    core_state_.volume.muted.set(!muted);
}
```

### With Protocol Messages

```cpp
void MacroValueHandler::handleValueChange(uint8_t index, float value) {
    // 1. Update state
    core_state_.setMacroValue(index, value);

    // 2. Send MIDI CC
    const auto& config = core_state_.getMacroConfig(index);
    uint8_t cc_value = static_cast<uint8_t>(value * 127.0f);
    midi_.sendCC(config.channel, config.cc, cc_value);

    // 3. Update status indicator
    core_state_.statusBar.ccOutActive.set(true);
}
```

### With Serial Protocol (Plugin)

```cpp
void ParameterHandler::handleValueChange(int index, float value) {
    // 1. Optimistic UI update
    state_.parameters[index].value.set(value);

    // 2. Send to host (host will confirm)
    protocol_.send(ParameterValueMessage{
        .index = index,
        .value = value,
        .fromHost = false  // Mark as user-initiated
    });
}
```

---

## Step 4: Register in Context

```cpp
// In context/StandaloneContext.cpp

void StandaloneContext::createInputHandlers() {
    // Create view first (for scope element)
    view_ = std::make_unique<MacroView>(view_container_->getMainZone(), core_state_);

    // Create handlers with view's scope
    input_handler_ = std::make_unique<MacroValueHandler>(
        core_state_,
        encoders(),
        midi(),
        view_->getElement()  // Scope element
    );

    transport_handler_ = std::make_unique<TransportHandler>(
        core_state_,
        encoders(),
        buttons(),
        view_->getElement()
    );
}
```

---

## Binding Patterns

### Scoped vs Global Bindings

```cpp
// SCOPED: Only active when scope element is visible
buttons_.button(ButtonID::NAV)
    .press()
    .scope(scope(overlay_element_))  // Tied to overlay
    .then([this]() { selectItem(); });

// GLOBAL: Always active (use sparingly)
buttons_.button(ButtonID::POWER)
    .press()
    .then([this]() { shutdown(); });  // No scope = global
```

### Multiple Encoders with Index

```cpp
void MacroValueHandler::setupBindings() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope(scope_element_))
            .then([this, i](float value) {  // Capture index by value!
                handleValueChange(i, value);
            });
    }
}
```

### Encoder Modes

```cpp
// Set encoder to relative mode (delta values)
encoders_.setMode(EncoderID::NAV, oc::hal::EncoderMode::RELATIVE);

// Binding receives delta (-1.0 to +1.0 per tick)
encoders_.encoder(EncoderID::NAV)
    .turn()
    .then([this](float delta) {
        // delta is typically -1.0, 0.0, or +1.0
        handleTempoChange(delta);
    });
```

```cpp
// Set encoder to absolute mode (0.0 to 1.0)
encoders_.setMode(EncoderID::VOLUME, oc::hal::EncoderMode::ABSOLUTE);

// Binding receives absolute value
encoders_.encoder(EncoderID::VOLUME)
    .turn()
    .then([this](float value) {
        // value is 0.0 to 1.0
        state_.volume.level.set(value);
    });
```

### Conditional Bindings

```cpp
// Only trigger when condition is met
buttons_.button(ButtonID::SELECT)
    .press()
    .scope(scope(scope_element_))
    .when([this]() { return hasSelection(); })  // Guard condition
    .then([this]() { confirmSelection(); });
```

---

## Complete Example

### TransportHandler

```cpp
// File: src/handler/transport/TransportHandler.hpp
#pragma once

/**
 * @file TransportHandler.hpp
 * @brief Handles transport controls (tempo, play/stop)
 *
 * - NAV encoder: tempo +/- 1 BPM
 * - BOTTOM_CENTER button: toggle play
 */

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"

namespace core::handler {

class TransportHandler {
public:
    TransportHandler(core::state::CoreState& coreState,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          lv_obj_t* scopeElement);

    ~TransportHandler() = default;

    TransportHandler(const TransportHandler&) = delete;
    TransportHandler& operator=(const TransportHandler&) = delete;

private:
    void setupBindings();
    void handleTempoChange(float delta);
    void handlePlayToggle();

    core::state::CoreState& core_state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* scope_element_;

    static constexpr float TEMPO_MIN = 20.0f;
    static constexpr float TEMPO_MAX = 300.0f;
};

}  // namespace core::handler
```

```cpp
// File: src/handler/transport/TransportHandler.cpp
#include "TransportHandler.hpp"

#include <algorithm>
#include <oc/hal/IEncoderController.hpp>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::handler {

using namespace oc::ui::lvgl;

TransportHandler::TransportHandler(
    core::state::CoreState& coreState,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* scopeElement)
    : core_state_(coreState)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_element_(scopeElement)
{
    setupBindings();
}

void TransportHandler::setupBindings() {
    // Set NAV encoder to relative mode
    encoders_.setMode(Config::EncoderID::NAV, oc::hal::EncoderMode::RELATIVE);

    // NAV encoder: tempo +/- 1 BPM per tick
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(scope_element_))
        .then([this](float delta) { handleTempoChange(delta); });

    // BOTTOM_CENTER button: toggle play
    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .press()
        .scope(scope(scope_element_))
        .then([this]() { handlePlayToggle(); });
}

void TransportHandler::handleTempoChange(float delta) {
    float currentTempo = core_state_.statusBar.tempo.get();
    float newTempo = std::clamp(currentTempo + delta, TEMPO_MIN, TEMPO_MAX);
    core_state_.statusBar.tempo.set(newTempo);
}

void TransportHandler::handlePlayToggle() {
    bool playing = core_state_.statusBar.playing.get();
    core_state_.statusBar.playing.set(!playing);
}

}  // namespace core::handler
```

---

## Checklist

Before committing a new handler:

- [ ] Header has `@file` and `@brief` documentation
- [ ] Constructor takes state and API references
- [ ] Constructor takes `scopeElement` for scoped bindings
- [ ] `setupBindings()` called in constructor
- [ ] All bindings use `.scope(scope(...))` unless truly global
- [ ] Action methods only call `state_.*.set()` and protocol/midi
- [ ] No `lv_*()` calls in handler code
- [ ] Loop captures use `[this, i]` (index by value)
- [ ] Rule of 5: copy/move deleted
- [ ] Handler registered in Context

---

## See Also

- [STATE_MANAGEMENT.md](STATE_MANAGEMENT.md) - State update patterns
- [INVARIANTS.md](INVARIANTS.md) - Handler boundary rules
- [HOW_TO_ADD_OVERLAY.md](HOW_TO_ADD_OVERLAY.md) - Overlay-specific handlers
