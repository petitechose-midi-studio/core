#include "PluginRegistry.hpp"

#include "manager/PluginManager.hpp"

void PluginRegistry::setup(PluginManager& manager) {
    // No plugins are hardcoded in core
    // Plugins should be registered by external libraries that link against core
    // Example (in plugin code):
    //   manager.registerPlugin<Plugin::MyPlugin::Plugin>("myplugin");
}