#include "MidiStudioApp.hpp"

#include "core/factory/InputFactory.hpp"
#include "core/factory/MidiFactory.hpp"
#include "core/event/Events.hpp"
#include "core/event/UnifiedEventTypes.hpp"
#include "log/Macros.hpp"

MidiStudioApp::MidiStudioApp(PluginSetupFn setupPlugins)
    : display_driver_(),
      setup_plugins_(setupPlugins),
      event_bus_(),
      multiplexer_(),
      encoders_config_(InputFactory::createEncoders()),
      buttons_config_(InputFactory::createButtons()),
      display_bridge_(display_driver_),
      midi_out_(event_bus_),
      midi_in_(event_bus_),
      encoders_(encoders_config_, event_bus_),
      buttons_(buttons_config_, multiplexer_, event_bus_),
      midi_mapper_(midi_out_, event_bus_, MidiFactory::createDefault()),
      ui_(display_bridge_, event_bus_),
      input_manager_(encoders_, buttons_),
      ui_controller_(ui_, event_bus_),
      plugins_(event_bus_, midi_in_, midi_out_, encoders_, ui_)
{
#ifdef DEBUG_LOGS
    waitForSerial();
#endif
}

MidiStudioApp::~MidiStudioApp() = default;

bool MidiStudioApp::setup()
{
    boot_complete_sub_ = event_bus_.on(EventCategory::System, SystemEvent::BootComplete,
                                    [this](const Event &e)
                                    { onBootComplete(e); });

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
        .eventBus = event_bus_};

    boot_manager_.emplace(components);
    return true;
}

void MidiStudioApp::update() {
    if (!boot_complete_)
    {
        runBootSequence();
    }
    else
    {
        runMainLoop();
    }
}

void MidiStudioApp::runBootSequence()
{
    if (!boot_manager_.has_value())
        return;

    if (boot_manager_->tick())
    {
        boot_complete_ = true;
        LOGLN("[App] Boot complete");
        boot_manager_.reset();
    }
}

void MidiStudioApp::runMainLoop()
{
    midi_in_.processPendingMessages();
    input_manager_.update();

    if (plugins_initialized_) {
        plugins_.update();
    }

    ui_.update();
}

void MidiStudioApp::initializePlugins() {
    if (plugins_initialized_) return;

    LOGLN("[App] Initializing plugins...");
    if (setup_plugins_) {
        setup_plugins_(plugins_);
        plugins_initialized_ = true;
    }
}

void MidiStudioApp::onBootComplete(const Event& event) {
    initializePlugins();
}
