#include "PluginManager.hpp"

#include <Arduino.h>

#include "adapter/input/encoder/EncoderController.hpp"
#include "adapter/midi/TeensyUsbMidiIn.hpp"
#include "core/input/InputBinding.hpp"

PluginManager::PluginManager(IEventBus& eventBus, TeensyUsbMidiIn& midiIn,
                             TeensyUsbMidiOut& midiOut, EncoderController& encoders,
                             ViewManager& viewManager)
    : bindingService_(eventBus),
      midiOut_(midiOut),
      api_(bindingService_, eventBus, midiOut_, encoders, viewManager) {}

PluginManager::~PluginManager() {
    for (auto& [name, plugin] : plugins_) {
        if (plugin) {
            plugin->cleanup();
        }
    }
    plugins_.clear();
}

void PluginManager::update() {
    bindingService_.processTick(millis());

    for (const auto& [name, plugin] : plugins_) {
        if (plugin && plugin->isEnabled()) {
            plugin->update();
        }
    }
}