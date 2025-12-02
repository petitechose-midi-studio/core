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
#include "core/event/EventBus.hpp"
#include "core/event/IEventBus.hpp"
#include "core/midi/MidiMapper.hpp"
#include "core/struct/Button.hpp"
#include "core/struct/Encoder.hpp"
#include "manager/InputManager.hpp"
#include "manager/PluginManager.hpp"
#include "manager/ViewManager.hpp"
#include "ui/ViewController.hpp"

class MidiStudioApp {
public:
    using PluginSetupFn = void (*)(PluginManager&);

    explicit MidiStudioApp(PluginSetupFn setupPlugins = nullptr);
    ~MidiStudioApp();

    bool setup();
    void update();

    bool isBootComplete() const { return boot_complete_; }
private:
    // Display
    Ili9341Driver display_driver_;

    // Core
    PluginSetupFn setup_plugins_;
    EventBus event_bus_;
    Multiplexer multiplexer_;
    std::vector<Hardware::Encoder> encoders_config_;
    std::vector<Hardware::Button> buttons_config_;

    // Bridge
    LVGLBridge display_bridge_;

    // MIDI + Input
    TeensyUsbMidiOut midi_out_;
    TeensyUsbMidiIn midi_in_;
    EncoderController encoders_;
    ButtonController buttons_;
    MidiMapper midi_mapper_;

    // Views
    ViewManager ui_;
    InputManager input_manager_;
    ViewController ui_controller_;

    // Plugins
    PluginManager plugins_;

    // Boot
    std::optional<Boot::BootManager> boot_manager_;
    bool boot_complete_ = false;
    bool plugins_initialized_ = false;
    SubscriptionId boot_complete_sub_ = 0;

    void initializePlugins();
    void onBootComplete(const Event& event);
    void runBootSequence();
    void runMainLoop();
};
