/*
 * PluginManager - Plugin system with minimal heap usage
 *
 * Services (InputBinding, MidiOutAdapter) are stack-allocated.
 * Only plugins themselves are heap-allocated for dynamic load/unload.
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "api/ControllerAPI.hpp"
#include "adapter/midi/TeensyUsbMidiOut.hpp"
#include "resource/common/interface/IPlugin.hpp"
#include "core/event/IEventBus.hpp"
#include "core/input/InputBinding.hpp"

class TeensyUsbMidiIn;
class EncoderController;
class LVGLBridge;

class PluginManager {
private:
    InputBinding bindingService_;
    TeensyUsbMidiOut& midiOut_;
    ControllerAPI api_;
    std::unordered_map<std::string, std::unique_ptr<IPlugin>> plugins_;

public:
    PluginManager(IEventBus& eventBus, TeensyUsbMidiIn& midiIn, TeensyUsbMidiOut& midiOut,
                  EncoderController& encoders, ViewManager& viewManager);

    ~PluginManager();

    ControllerAPI& getServices() {
        return api_;
    }

    template <typename PluginType>
    bool registerPlugin(const std::string& name) {
        static_assert(std::is_base_of_v<IPlugin, PluginType>,
                      "PluginType must inherit from IIntegrationPlugin");

        if (plugins_.find(name) != plugins_.end()) {
            return false;
        }

        auto plugin = std::make_unique<PluginType>(api_);
        if (!plugin->initialize()) {
            return false;
        }

        plugins_[name] = std::move(plugin);
        return true;
    }

    void update();
};
