#pragma once

#include "UnifiedEventTypes.hpp"

class Event {
public:
    Event(EventCategoryType category, EventType type) : category_(category), type_(type) {}

    ~Event() = default;

    EventCategoryType getCategory() const {
        return category_;
    }

    EventType getType() const {
        return type_;
    }

protected:
    EventCategoryType category_;
    EventType type_;
};
