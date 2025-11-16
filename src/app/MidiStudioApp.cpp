#include "MidiStudioApp.hpp"

#include "core/factory/InputFactory.hpp"
#include "core/factory/MidiFactory.hpp"
#include "core/event/Events.hpp"
#include "core/event/UnifiedEventTypes.hpp"
#include "log/Macros.hpp"

/*
 * Constructor - Full stack allocation following dependency levels
 */
MidiStudioApp::MidiStudioApp(PluginSetupFn setupPlugins)

    : setupPlugins_(setupPlugins),
      eventBus_(),
      displayDriver_(),
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
      plugins_(eventBus_, midiIn_, midiOut_, encoders_, ui_) {

    // S'abonner à l'événement BootComplete pour initialiser les plugins après le splash
    bootCompleteSub_ = eventBus_.on(EventCategory::System, SystemEvent::BootComplete,
        [this](const Event& e) {
            onBootComplete(e);
        });

    ready_ = true;
}

MidiStudioApp::~MidiStudioApp() = default;

/*
 * Initialization - All components already fully initialized in constructor (RAII)
 */
bool MidiStudioApp::setup() {
    return true;
}

/*
 * Main Loop
 */
void MidiStudioApp::update() {
    if (!ready_) return;

    midiIn_.processPendingMessages();
    inputManager_.update();

    if (pluginsInitialized_) {
        plugins_.update();
    }

    ui_.update();
}

void MidiStudioApp::initializePlugins() {
    if (pluginsInitialized_) return;

    LOGLN("[MidiStudioApp] Boot complete - Initializing plugins...");

    if (setupPlugins_) {
        setupPlugins_(plugins_);
    }

    pluginsInitialized_ = true;
    LOGLN("[MidiStudioApp] Plugins initialized");
}

void MidiStudioApp::onBootComplete(const Event& event) {
    initializePlugins();
}
