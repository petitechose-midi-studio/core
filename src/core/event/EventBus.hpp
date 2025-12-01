#pragma once

#include <map>
#include <memory>
#include <vector>

#include "Event.hpp"
#include "IEventBus.hpp"
#include "UnifiedEventTypes.hpp"

class EventBus : public IEventBus {
public:
    EventBus() : nextId_(1) {}

    SubscriptionId on(EventCategoryType category, EventType type, EventCallback callback) override {
        if (!callback) {
            return 0;
        }

        uint32_t key = makeKey(category, type);
        SubscriptionId id = nextId_++;

        callbackSubscriptions_[key].push_back({id, callback});

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

    using CallbackList = std::vector<CallbackSubscription>;
    using SubscriptionMap = std::map<uint32_t, CallbackList>;

    uint32_t makeKey(EventCategoryType category, EventType type) const {
        return (static_cast<uint32_t>(category) << 16) | type;
    }

    SubscriptionMap callbackSubscriptions_;
    SubscriptionId nextId_;
};
