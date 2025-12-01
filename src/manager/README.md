# Manager

Orchestration components for MIDI Studio Core subsystems.

## Overview

Managers coordinate specific subsystems without containing business logic themselves:

- **BootManager** — Phased boot sequence with progress
- **ViewManager** — LVGL screen management
- **InputManager** — Encoder and button coordination
- **PluginManager** — Plugin registration and lifecycle

## Files

| File | Description |
|------|-------------|
| `BootManager.hpp/.cpp` | Boot sequence state machine |
| `ViewManager.hpp/.cpp` | Screen and view management |
| `InputManager.hpp/.cpp` | Input device coordination |
| `PluginManager.hpp/.cpp` | Plugin lifecycle management |

---

## BootManager

Manages the boot sequence in distinct phases with visual progress.

### Boot Phases

```cpp
enum class Phase {
    NotStarted,
    HardwareInit,    // Multiplexer, display driver, encoders
    DisplayInit,     // LVGL initialization
    MinimalUI,       // Splash screen with essential fonts
    LoadingFonts,    // Progressive font loading
    InputInit,       // Flush encoder events
    MidiInit,        // USB MIDI initialization
    Ready            // Boot complete → main loop
};
```

### Usage

```cpp
Boot::BootManager::Components components{
    .displayDriver = display_driver_,
    .lvglBridge = display_bridge_,
    .viewManager = ui_,
    .multiplexer = multiplexer_,
    .encoders = encoders_,
    .buttons = buttons_,
    .midiIn = midi_in_,
    .midiOut = midi_out_,
    .inputManager = input_manager_,
    .eventBus = event_bus_
};

boot_manager_.emplace(components);

// In main loop
while (!boot_manager_->tick()) {
    // Boot in progress
}
// Boot complete
```

### Interface

```cpp
class BootManager {
public:
    explicit BootManager(Components components);

    bool tick();           // Advance boot, returns true when complete
    bool isComplete() const;
};
```

**Events emitted:**
- `SystemBootCompleteEvent` — When boot finishes

---

## ViewManager

Manages LVGL screens and view lifecycle.

### Screen Architecture

```
┌─────────────────┐     ┌─────────────────┐
│   coreScreen_   │     │  pluginScreen_  │
├─────────────────┤     ├─────────────────┤
│  SplashScreen   │     │   Plugin View   │
│  (boot/idle)    │     │   (active)      │
└─────────────────┘     └─────────────────┘
        ↑                       ↑
        └───── lv_scr_load() ───┘
```

### Interface

```cpp
class ViewManager {
public:
    ViewManager(LVGLBridge& displayBridge, IEventBus& eventBus);

    // Initialization
    void initScreens();        // Create core and plugin screens
    void initSplash();         // Create splash view on core screen

    // Main loop
    void update();             // Refresh display if needed

    // Plugin view management
    lv_obj_t* getPluginContainer();  // Get plugin screen
    void showPluginView(IView& view);  // Switch to plugin screen
    void hidePluginView();             // Return to core screen

    // Core view management
    SplashScreenView* getSplashView();
    void showCoreSplash();
    void hideCoreSplash();

    // Boot
    void emitBootComplete();   // Emit SystemBootCompleteEvent
};
```

### View Lifecycle

1. Plugin calls `showPluginView(myView)`
2. ViewManager calls `myView.onActivate()`
3. ViewManager switches to plugin screen
4. On `hidePluginView()`:
   - ViewManager calls `myView.onDeactivate()`
   - Switches back to core screen

---

## InputManager

Coordinates encoder and button input processing.

### Interface

```cpp
class InputManager {
public:
    InputManager(EncoderController& encoders, ButtonController& buttons);

    void update();  // Poll buttons, flush encoder events
};
```

### Update Loop

```cpp
void InputManager::update() {
    // Flush batched encoder events (from ISR)
    encoders_.flushAllEvents();

    // Poll button states
    buttons_.updateAll();
}
```

---

## PluginManager

Manages plugin registration, initialization, and lifecycle.

### Interface

```cpp
class PluginManager {
public:
    PluginManager(
        IEventBus& eventBus,
        TeensyUsbMidiIn& midiIn,
        TeensyUsbMidiOut& midiOut,
        EncoderController& encoders,
        ViewManager& viewManager);

    ~PluginManager();

    // Registration
    template<typename T>
    void registerPlugin(const char* name);

    // Access
    ControllerAPI& getServices();

    // Main loop
    void update();  // Call plugin.update() for each plugin
};
```

### Registration

```cpp
template<typename T>
void PluginManager::registerPlugin(const char* name) {
    // Creates plugin with ControllerAPI injection
    auto plugin = std::make_unique<T>(api_);

    // Initialize plugin
    if (plugin->initialize()) {
        plugins_.push_back(std::move(plugin));
    }
}
```

### Plugin Lifecycle

1. **Registration** — `registerPlugin<T>(name)` called during setup
2. **Construction** — Plugin receives `ControllerAPI&`
3. **Initialization** — `plugin.initialize()` called
4. **Update Loop** — `plugin.update()` called each frame
5. **Cleanup** — `plugin.cleanup()` called on destruction

### Resource Loading

Plugins can implement static `loadResources()` for early resource loading:

```cpp
class MyPlugin : public IPlugin {
public:
    // Called during boot if present
    static void loadResources() {
        // Load fonts, images, etc.
    }
};
```

Detection uses SFINAE:

```cpp
template<typename T, typename = void>
struct has_load_resources : std::false_type {};

template<typename T>
struct has_load_resources<T, std::void_t<decltype(T::loadResources())>>
    : std::true_type {};
```

---

## Interaction Flow

```
MidiStudioApp::update()
        │
        ▼
┌───────────────────┐
│ boot_complete_?   │──No──► BootManager::tick()
└───────────────────┘
        │Yes
        ▼
┌───────────────────┐
│ midi_in_.process  │  ← Process incoming MIDI
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ input_manager_    │  ← Poll buttons, flush encoders
│    .update()      │
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ plugins_.update() │  ← Update all plugins
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ ui_.update()      │  ← Refresh display
└───────────────────┘
```

---

## See Also

- [Architecture](../../docs/ARCHITECTURE.md) — Overall system design
- [Boot Sequence](../../docs/ARCHITECTURE.md#state-machine-boot) — Boot phases
- [Plugin Development](../../docs/PLUGIN_DEVELOPMENT.md) — Creating plugins
