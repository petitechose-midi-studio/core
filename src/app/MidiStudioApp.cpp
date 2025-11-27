#include "MidiStudioApp.hpp"

#include "core/factory/InputFactory.hpp"
#include "core/factory/MidiFactory.hpp"
#include "core/event/Events.hpp"
#include "core/event/UnifiedEventTypes.hpp"
#include "log/Macros.hpp"

MidiStudioApp::MidiStudioApp(PluginSetupFn setupPlugins)
    : displayDriver_(),
      setupPlugins_(setupPlugins),
      eventBus_(),
      multiplexer_(),
      encoders_config_(InputFactory::createEncoders()),
      buttons_config_(InputFactory::createButtons()),
      displayBridge_(displayDriver_),
      midiOut_(eventBus_),
      midiIn_(eventBus_),
      encoders_(encoders_config_, eventBus_),
      buttons_(buttons_config_, multiplexer_, eventBus_),
      midiMapper_(midiOut_, eventBus_, MidiFactory::createDefault()),
      ui_(displayBridge_, eventBus_),
      inputManager_(encoders_, buttons_),
      uiController_(ui_, eventBus_),
      plugins_(eventBus_, midiIn_, midiOut_, encoders_, ui_)
{
    LOGLN("[App] Created");
}

MidiStudioApp::~MidiStudioApp() = default;

bool MidiStudioApp::setup() {
    delay(System::Application::INIT_BOOT_DELAY);
    LOGLN("[App] Starting boot...");

    bootCompleteSub_ = eventBus_.on(EventCategory::System, SystemEvent::BootComplete,
                                    [this](const Event &e)
                                    { onBootComplete(e); });

    Boot::BootManager::Components components{
        .displayDriver = displayDriver_,
        .lvglBridge = displayBridge_,
        .viewManager = ui_,
        .encoders = encoders_,
        .buttons = buttons_,
        .midiIn = midiIn_,
        .midiOut = midiOut_,
        .inputManager = inputManager_,
        .eventBus = eventBus_};

    bootManager_.emplace(components);
    return true;
}

void MidiStudioApp::update() {
    if (!bootComplete_)
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
    if (!bootManager_.has_value())
        return;

    if (bootManager_->tick())
    {
        bootComplete_ = true;
        LOGLN("[App] Boot complete");
        bootManager_.reset();
    }
}

void MidiStudioApp::runMainLoop()
{
    midiIn_.processPendingMessages();
    inputManager_.update();

    if (pluginsInitialized_) {
        plugins_.update();
    }

    ui_.update();
}

void MidiStudioApp::initializePlugins() {
    if (pluginsInitialized_) return;

    LOGLN("[App] Initializing plugins...");
    if (setupPlugins_) {
        setupPlugins_(plugins_);
        pluginsInitialized_ = true;
    }
}

void MidiStudioApp::onBootComplete(const Event& event) {
    initializePlugins();
}
