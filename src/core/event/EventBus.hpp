#pragma once

#include <etl/map.h>
#include <etl/vector.h>

#include <memory>

#include "Event.hpp"
#include "IEventBus.hpp"
#include "UnifiedEventTypes.hpp"
#include "config/System.hpp"

class EventBus : public IEventBus {
public:
    EventBus() : nextId_(1) {}

    SubscriptionId on(EventCategoryType category, EventType type, EventCallback callback) override {
        if (!callback) {
            return 0;
        }

        uint32_t key = makeKey(category, type);
        SubscriptionId id = nextId_++;

        auto it = callbackSubscriptions_.find(key);
        if (it == callbackSubscriptions_.end()) {
            CallbackList list;
            list.push_back({id, callback});
            callbackSubscriptions_[key] = list;
        } else {
            if (it->second.size() >= System::Memory::MAX_CALLBACKS_PER_EVENT) {
                return 0;
            }
            it->second.push_back({id, callback});
        }

        return id;
    }

    void emit(const Event& event) override {
        uint32_t key = makeKey(event.getCategory(), event.getType());
        auto it = callbackSubscriptions_.find(key);
        if (it != callbackSubscriptions_.end()) {
            for (const auto& sub : it->second) {
                sub.callback(event);
            }
        }
    }

    void off(SubscriptionId id) override {
        for (auto& pair : callbackSubscriptions_) {
            auto& list = pair.second;
            for (auto it = list.begin(); it != list.end(); ++it) {
                if (it->id == id) {
                    list.erase(it);
                    return;
                }
            }
        }
    }

    void clear() {
        callbackSubscriptions_.clear();
        nextId_ = 1;
    }

    size_t getSubscriberCount() const {
        size_t count = 0;
        for (const auto& pair : callbackSubscriptions_) {
            count += pair.second.size();
        }
        return count;
    }

private:
    struct CallbackSubscription {
        SubscriptionId id;
        EventCallback callback;
    };

    using CallbackList = etl::vector<CallbackSubscription, System::Memory::MAX_CALLBACKS_PER_EVENT>;
    using SubscriptionMap = etl::map<uint32_t, CallbackList, System::Memory::MAX_EVENT_TYPES>;

    uint32_t makeKey(EventCategoryType category, EventType type) const {
        return (static_cast<uint32_t>(category) << 16) | type;
    }

    SubscriptionMap callbackSubscriptions_;
    SubscriptionId nextId_;
};
