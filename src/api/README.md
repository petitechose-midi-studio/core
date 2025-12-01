# API

Plugin interface for interacting with MIDI Studio Core.

## Overview

The `ControllerAPI` class is the single entry point for plugins to interact with Core. It provides:

- **Input Binding** — React to buttons, encoders, gestures
- **MIDI I/O** — Send and receive MIDI messages
- **Encoder Control** — Configure encoder behavior
- **View Management** — Display plugin views
- **Logging** — Debug output

## Files

| File | Description |
|------|-------------|
| `ControllerAPI.hpp` | Main API class declaration |
| `ControllerAPI.cpp` | Implementation |

## ControllerAPI

### Constructor

```cpp
ControllerAPI(
    InputBinding& bindings,
    IEventBus& events,
    TeensyUsbMidiOut& midiOut,
    EncoderController& encoders,
    ViewManager& viewManager
);
```

Plugins receive a `ControllerAPI&` via constructor injection from `PluginManager`.

### Input Binding Methods

#### Button Events

| Method | Description |
|--------|-------------|
| `onPressed(ButtonID, callback)` | React to button press |
| `onReleased(ButtonID, callback)` | React to button release |
| `onLongPress(ButtonID, callback, ms)` | React to long press (default 500ms) |
| `onDoubleTap(ButtonID, callback)` | React to double tap |
| `onCombo(ButtonID, ButtonID, callback)` | React to two buttons pressed |

#### Encoder Events

| Method | Description |
|--------|-------------|
| `onTurned(EncoderID, callback)` | React to encoder rotation |
| `onTurnedWhilePressed(EncoderID, ButtonID, callback)` | React while button held |

#### Scoped Variants

All input methods have scoped variants with `lv_obj_t* scope` parameter:

```cpp
void onPressed(ButtonID, callback, lv_obj_t* scope, bool latch = false);
void onTurned(EncoderID, callback, lv_obj_t* scope);
// etc.
```

Scoped bindings are only active when the LVGL object is visible.

#### Scope Management

| Method | Description |
|--------|-------------|
| `clearScope(lv_obj_t*)` | Remove all bindings for a scope |
| `isLatched(ButtonID)` | Check if button is latched ON |
| `setLatch(ButtonID, bool)` | Manually set latch state |

### MIDI Input Methods

```cpp
template<typename Callback>
void onSysEx(Callback callback);   // void(const uint8_t*, uint16_t)

template<typename Callback>
void onCC(Callback callback);      // void(uint8_t ch, uint8_t cc, uint8_t val)

template<typename Callback>
void onNoteOn(Callback callback);  // void(uint8_t ch, uint8_t note, uint8_t vel)

template<typename Callback>
void onNoteOff(Callback callback); // void(uint8_t ch, uint8_t note, uint8_t vel)
```

### MIDI Output Methods

| Method | Description |
|--------|-------------|
| `sendCC(channel, cc, value)` | Send Control Change |
| `sendNoteOn(channel, note, velocity)` | Send Note On |
| `sendNoteOff(channel, note, velocity)` | Send Note Off |
| `sendSysEx(data, length)` | Send System Exclusive |

### Encoder Control Methods

| Method | Description |
|--------|-------------|
| `setEncoderPosition(id, value)` | Set encoder position (0.0-1.0) |
| `setEncoderDiscreteSteps(id, steps)` | Configure discrete steps |
| `setEncoderContinuous(id)` | Back to continuous mode |
| `setEncoderMode(id, mode)` | Set Absolute/Relative mode |
| `setEncoderBounds(id, min, max)` | Set value bounds |
| `setEncoderDelta(id, delta)` | Set delta per detent (Relative) |

### View Management Methods

| Method | Description |
|--------|-------------|
| `getParentContainer()` | Get plugin screen for UI creation |
| `showPluginView(IView&)` | Display a plugin view |
| `hidePluginView()` | Return to core screen |

### Logging Methods

| Method | Description |
|--------|-------------|
| `log(message)` | Log string (debug only) |
| `logf(format, ...)` | Log formatted (debug only) |

## Usage Example

```cpp
class MyPlugin : public IPlugin
{
public:
    explicit MyPlugin(ControllerAPI& api) : api_(api) {}

    bool initialize() override {
        // Create view
        view_ = std::make_unique<MyView>(api_.getParentContainer());
        api_.showPluginView(*view_);

        // Setup bindings
        api_.onTurned(EncoderID::MACRO_1, [this](float v) {
            api_.sendCC(0, 1, static_cast<uint8_t>(v * 127));
            view_->updateKnob(0, v);
        }, view_->getContainer());

        api_.onSysEx([this](const uint8_t* data, uint16_t len) {
            handleSysEx(data, len);
        });

        return true;
    }

private:
    ControllerAPI& api_;
    std::unique_ptr<MyView> view_;
};
```

## See Also

- [Plugin Development Guide](../../docs/PLUGIN_DEVELOPMENT.md)
- [Input IDs](../config/InputID.hpp)
- [InputBinding](../core/input/InputBinding.hpp)
