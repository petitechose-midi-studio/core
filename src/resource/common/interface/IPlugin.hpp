#pragma once

#include <cstddef>
#include <cstdint>

class ControllerAPI;

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual bool initialize() = 0;
    virtual void cleanup() = 0;
    virtual void update() = 0;

    virtual const char* getName() const = 0;
    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
};