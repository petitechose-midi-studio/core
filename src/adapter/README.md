# Adapter

Hardware abstraction layer for MIDI Studio Core.

## Overview

Adapters provide hardware-specific implementations behind abstract interfaces. This enables:

- **Portability** — Different hardware = different adapter
- **Testability** — Mock adapters for unit tests
- **Decoupling** — Business logic independent of hardware

## Directory Structure

```
adapter/
├── display/
│   ├── driver/
│   │   ├── Ili9341Driver.hpp    # ILI9341 display driver
│   │   └── Ili9341Driver.cpp
│   └── ui/
│       ├── LVGLBridge.hpp       # LVGL ↔ Driver interface
│       ├── LVGLBridge.cpp
│       ├── LVGLMemory.hpp       # LVGL memory management
│       └── LVGLMemory.cpp
├── input/
│   ├── button/
│   │   ├── ButtonController.hpp # Button management
│   │   ├── ButtonController.cpp
│   │   ├── ButtonFactory.hpp    # Button creation
│   │   ├── UnifiedButton.hpp    # Single button abstraction
│   │   └── reader/
│   │       ├── IPinReader.hpp       # Pin reader interface
│   │       ├── TeensyPinReader.hpp  # Direct MCU pins
│   │       └── TeensyMultiplexerPinReader.hpp  # Mux pins
│   └── encoder/
│       ├── Encoder.hpp          # Single encoder
│       ├── Encoder.cpp
│       ├── EncoderController.hpp # Encoder management
│       └── EncoderController.cpp
├── midi/
│   ├── TeensyUsbMidiIn.hpp      # USB MIDI input
│   ├── TeensyUsbMidiIn.cpp
│   ├── TeensyUsbMidiOut.hpp     # USB MIDI output
│   └── TeensyUsbMidiOut.cpp
└── multiplexer/
    ├── MultiplexerController.hpp # CD74HC4067 driver
    └── MultiplexerController.cpp
```

---

## Display

### Ili9341Driver

Low-level driver for ILI9341 TFT display using ILI9341_T4 library.

```cpp
class Ili9341Driver {
public:
    void init();
    void update(const uint16_t* buffer);

    ILI9341_T4::ILI9341Driver& getDriver();
};
```

**Configuration** (from `System::Hardware`):
- SPI pins: CS=28, DC=0, RST=29, MOSI=26, SCK=27, MISO=1
- Speed: 20 MHz
- Resolution: 320×240

### LVGLBridge

Bridges LVGL to the display driver.

```cpp
class LVGLBridge {
public:
    explicit LVGLBridge(Ili9341Driver& driver);

    void init();           // Initialize LVGL
    bool isInitialized() const;
    void refresh();        // Process LVGL tasks and update display

private:
    static void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px);
};
```

### LVGLMemory

Custom memory allocation for LVGL using EXTMEM (PSRAM).

---

## Input

### EncoderController

Manages all hardware encoders.

```cpp
class EncoderController {
public:
    explicit EncoderController(
        const std::vector<Hardware::Encoder>& encoderSetups,
        IEventBus& eventBus);

    void init();                  // Initialize hardware (call after setup())
    void flushAllEvents();        // Emit pending events (batch processing)

    // Encoder configuration
    void resetEncoderPosition(EncoderID id, float normalizedValue);
    void setDiscreteSteps(EncoderID id, uint16_t steps);
    void setContinuous(EncoderID id);
    void setMode(EncoderID id, Hardware::EncoderMode mode);
    void setBounds(EncoderID id, float min, float max);
    void setDelta(EncoderID id, float delta);

    Encoder* getEncoder(EncoderID id);
};
```

### Encoder

Individual encoder with ISR-based position tracking.

```cpp
class Encoder {
public:
    Encoder(const Hardware::Encoder& setup, IEventBus& eventBus);

    void init();          // Initialize hardware
    void flushEvents();   // Emit pending event

    void resetPosition(float normalizedValue);
    void setDiscreteSteps(uint8_t steps);
    void setContinuous();
    void setMode(Hardware::EncoderMode mode);
    void setBounds(float min, float max);
    void setDelta(float delta);
};
```

**Modes:**
- `Absolute` — Normalized value 0.0-1.0 with software stops
- `Relative` — Infinite rotation, emits ±delta per detent

### ButtonController

Manages all hardware buttons.

```cpp
class ButtonController {
public:
    explicit ButtonController(
        const std::vector<Hardware::Button>& buttonSetups,
        Multiplexer& mux,
        IEventBus& eventBus);

    void updateAll();  // Poll all buttons, emit events

    UnifiedButton* getButton(ButtonID id);
};
```

### IPinReader

Interface for reading pin state (enables different hardware backends).

```cpp
class IPinReader {
public:
    virtual ~IPinReader() = default;
    virtual bool read() = 0;
};
```

**Implementations:**
- `TeensyPinReader` — Direct MCU GPIO
- `TeensyMultiplexerPinReader` — Via CD74HC4067

---

## MIDI

### TeensyUsbMidiOut

USB MIDI output using Teensy's native USB MIDI.

```cpp
class TeensyUsbMidiOut : public IMidiOutput {
public:
    explicit TeensyUsbMidiOut(IEventBus& eventBus);

    void sendControlChange(MidiChannelValue ch, MidiCCValue cc, uint8_t value) override;
    void sendNoteOn(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) override;
    void sendNoteOff(MidiChannelValue ch, MidiNoteValue note, uint8_t velocity) override;
    void sendProgramChange(MidiChannelValue ch, uint8_t program) override;
    void sendPitchBend(MidiChannelValue ch, uint16_t value) override;
    void sendChannelPressure(MidiChannelValue ch, uint8_t pressure) override;
    void sendSysEx(const uint8_t* data, uint16_t length) override;
    void flush();  // Additional method (not from interface)
};
```

**Features:**
- Active note tracking (up to 16 notes)
- SysEx buffer up to 16KB (configurable)

### TeensyUsbMidiIn

USB MIDI input with event bus integration.

```cpp
class TeensyUsbMidiIn : public IMidiInput {
public:
    explicit TeensyUsbMidiIn(IEventBus& eventBus);

    void init();
    void processPendingMessages();  // Call in main loop
};
```

Emits events:
- `MidiCCEvent`
- `MidiNoteOnEvent`
- `MidiNoteOffEvent`
- `SysExEvent`

---

## Multiplexer

### MultiplexerController

Driver for CD74HC4067 16-channel analog multiplexer.

```cpp
class Multiplexer {
public:
    void init();
    void selectChannel(uint8_t channel);
    bool readCurrentChannel();

    bool readChannel(uint8_t channel);  // Select + read
};
```

**Configuration** (from `System::Hardware`):
- Select pins: S0=3, S1=2, S2=5, S3=6
- Signal pin: 4
- Debounce: 20µs settle time

---

## Creating a New Adapter

1. **Define interface** in `core/interface/` if needed
2. **Create adapter** in appropriate subdirectory
3. **Inject via constructor** in `MidiStudioApp`

Example for a different display:

```cpp
// adapter/display/driver/ST7789Driver.hpp
class ST7789Driver {
public:
    void init();
    void update(const uint16_t* buffer);
    // ...
};

// In MidiStudioApp, replace Ili9341Driver with ST7789Driver
```

---

## See Also

- [Architecture](../../docs/ARCHITECTURE.md) — Adapter pattern details
- [Config](../config/) — Hardware pin definitions
- [Core Interfaces](../core/interface/) — Abstract interfaces
