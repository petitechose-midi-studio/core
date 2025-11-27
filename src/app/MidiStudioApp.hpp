/*
 * MidiStudioApp - Application entry point
 *
 * Owns all subsystems (stack-allocated for zero overhead):
 * Display, Input, MIDI, UI, Plugins
 */

#pragma once

#include <etl/vector.h>

#include "adapter/display/driver/Ili9341Driver.hpp"
#include "adapter/display/ui/LVGLBridge.hpp"
#include "adapter/input/button/ButtonController.hpp"
#include "adapter/input/encoder/EncoderController.hpp"
#include "adapter/midi/TeensyUsbMidiIn.hpp"
#include "adapter/midi/TeensyUsbMidiOut.hpp"
#include "adapter/multiplexer/MultiplexerController.hpp"
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

private:
    // 1. Display (first - needed by everything)
    Ili9341Driver displayDriver_;

    // 6. Plugins (last - depends on everything)
    // 2. Core + Hardware config
    PluginSetupFn setupPlugins_;

    EventBus eventBus_;
    Multiplexer multiplexer_;
    etl::vector<Hardware::Encoder, System::Hardware::ENCODERS_COUNT> encoders_config_;
    etl::vector<Hardware::Button, System::Hardware::BUTTONS_COUNT> buttons_config_;

    // 3. Bridge (after display, before views)
    LVGLBridge displayBridge_;

    // 4. MIDI + Input controllers
    TeensyUsbMidiOut midiOut_;
    TeensyUsbMidiIn midiIn_;
    EncoderController encoders_;
    ButtonController buttons_;
    MidiMapper midiMapper_;

    // 5. Views
    ViewManager ui_;
    InputManager inputManager_;
    ViewController uiController_;
    PluginManager plugins_;

    bool ready_ = false;
    bool pluginsInitialized_ = false;
    SubscriptionId bootCompleteSub_ = 0;

    void initializePlugins();
    void onBootComplete(const Event& event);
};
