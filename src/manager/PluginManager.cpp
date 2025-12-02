#include "PluginManager.hpp"

#include <Arduino.h>

#include "adapter/input/encoder/EncoderController.hpp"
#include "adapter/midi/TeensyUsbMidiIn.hpp"
#include "core/input/InputBinding.hpp"

PluginManager::PluginManager(IEventBus& eventBus, TeensyUsbMidiIn& midiIn,
                             TeensyUsbMidiOut& midiOut, EncoderController& encoders,
                             ViewManager& viewManager)
    : binding_service_(eventBus), midi_out_(midiOut),
      api_(binding_service_, eventBus, midi_out_, encoders, viewManager) {}

PluginManager::~PluginManager() {
    for (auto& [name, plugin] : plugins_) {
        if (plugin) { plugin->cleanup(); }
    }
    plugins_.clear();
}

void PluginManager::update() {
    binding_service_.processTick(millis());

    for (const auto& [name, plugin] : plugins_) {
        if (plugin && plugin->isEnabled()) { plugin->update(); }
    }
}