#pragma once

class IPlugin;
class PluginManager;

/**
 * @brief Factory for registering all available integration plugins
 *
 * This keeps plugin-specific knowledge out of the core application
 * while allowing the PluginManager to discover available plugins
 */
class PluginRegistry {
public:
    /**
     * @brief Register all available plugins with the manager
     * @param manager The integration manager to register plugins with
     *
     */
    static void setup(PluginManager& manager);
};