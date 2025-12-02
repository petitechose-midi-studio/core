/*
 * PluginManager - Plugin system with minimal heap usage
 *
 * Services (InputBinding, MidiOutAdapter) are stack-allocated.
 * Only plugins themselves are heap-allocated for dynamic load/unload.
 */

#pragma once

#include <memory>
#include <string>

#include <type_traits>
#include <unordered_map>

#include "adapter/midi/TeensyUsbMidiOut.hpp"
#include "api/ControllerAPI.hpp"
#include "core/event/IEventBus.hpp"
#include "core/input/InputBinding.hpp"
#include "font/FontLoader.hpp"
#include "resource/common/interface/IPlugin.hpp"

class TeensyUsbMidiIn;
class EncoderController;
class LVGLBridge;

// SFINAE helper: detect if T::loadResources() exists
template <typename T, typename = void>
struct has_load_resources : std::false_type {};

template <typename T>
struct has_load_resources<T, std::void_t<decltype(T::loadResources())>> : std::true_type {};

class PluginManager {
private:
    InputBinding binding_service_;
    TeensyUsbMidiOut& midi_out_;
    ControllerAPI api_;
    std::unordered_map<std::string, std::unique_ptr<IPlugin>> plugins_;
public:
    PluginManager(IEventBus& eventBus, TeensyUsbMidiIn& midiIn, TeensyUsbMidiOut& midiOut,
                  EncoderController& encoders, ViewManager& viewManager);

    ~PluginManager();

    ControllerAPI& getServices() { return api_; }

    template <typename PluginType>
    bool registerPlugin(const std::string& name) {
        static_assert(std::is_base_of_v<IPlugin, PluginType>,
                      "PluginType must inherit from IIntegrationPlugin");

        if (plugins_.find(name) != plugins_.end()) { return false; }

        // Load plugin resources and fonts (if loadResources() exists)
        if constexpr (has_load_resources<PluginType>::value) {
            PluginType::loadResources();
            loadPluginFonts();
        }

        auto plugin = std::make_unique<PluginType>(api_);
        if (!plugin->initialize()) { return false; }

        plugins_[name] = std::move(plugin);
        return true;
    }

    void update();
};
