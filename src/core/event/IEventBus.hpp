#pragma once

#include <functional>

#include "Event.hpp"

using SubscriptionId = uint8_t;
using EventCallback = std::function<void(const Event&)>;

class IEventBus {
protected:
    ~IEventBus() = default;

public:
    virtual SubscriptionId on(EventCategoryType category, EventType type,
                              EventCallback callback) = 0;
    virtual void emit(const Event& event) = 0;
    virtual void off(SubscriptionId id) = 0;
};
