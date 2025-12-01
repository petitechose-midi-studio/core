#pragma once

#include <optional>
#include <vector>

#include "adapter/display/driver/Ili9341Driver.hpp"
#include "adapter/display/ui/LVGLBridge.hpp"
#include "adapter/input/button/ButtonController.hpp"
#include "adapter/input/encoder/EncoderController.hpp"
#include "adapter/midi/TeensyUsbMidiIn.hpp"
#include "adapter/midi/TeensyUsbMidiOut.hpp"
#include "adapter/multiplexer/MultiplexerController.hpp"
#include "boot/BootManager.hpp"
#include "config/System.hpp"
#include "core/event/EventBus.hpp"
#include "core/event/IEventBus.hpp"
#include "core/midi/MidiMapper.hpp"
#include "core/struct/Button.hpp"
#include "core/struct/Encoder.hpp"
#include "manager/PluginManager.hpp"
#include "manager/InputManager.hpp"
#include "manager/ViewManager.hpp"
#include "ui/ViewController.hpp"

class MidiStudioApp {
public:
    using PluginSetupFn = void (*)(PluginManager&);

    explicit MidiStudioApp(PluginSetupFn setupPlugins = nullptr);
    ~MidiStudioApp();

    bool setup();
    void update();

    bool isBootComplete() const { return bootComplete_; }

private:
    // Display
    Ili9341Driver displayDriver_;

    // Core
    PluginSetupFn setupPlugins_;
    EventBus eventBus_;
    Multiplexer multiplexer_;
    std::vector<Hardware::Encoder> encoders_config_;
    std::vector<Hardware::Button> buttons_config_;

    // Bridge
    LVGLBridge displayBridge_;

    // MIDI + Input
    TeensyUsbMidiOut midiOut_;
    TeensyUsbMidiIn midiIn_;
    EncoderController encoders_;
    ButtonController buttons_;
    MidiMapper midiMapper_;

    // Views
    ViewManager ui_;
    InputManager inputManager_;
    ViewController uiController_;

    // Plugins
    PluginManager plugins_;

    // Boot
    std::optional<Boot::BootManager> bootManager_;
    bool bootComplete_ = false;
    bool pluginsInitialized_ = false;
    SubscriptionId bootCompleteSub_ = 0;

    void initializePlugins();
    void onBootComplete(const Event& event);
    void runBootSequence();
    void runMainLoop();
};
