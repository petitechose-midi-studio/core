#pragma once

#include <map>
#include <memory>
#include <vector>

#include "Event.hpp"
#include "IEventBus.hpp"
#include "UnifiedEventTypes.hpp"

class EventBus : public IEventBus {
public:
    EventBus() : next_id_(1) {}

    SubscriptionId on(EventCategoryType category, EventType type, EventCallback callback) override {
        if (!callback) {
            return 0;
        }

        uint32_t key = makeKey(category, type);
        SubscriptionId id = next_id_++;

        callback_subscriptions_[key].push_back({id, callback});

        return id;
    }

    void emit(const Event& event) override {
        uint32_t key = makeKey(event.getCategory(), event.getType());
        auto it = callback_subscriptions_.find(key);
        if (it != callback_subscriptions_.end()) {
            for (const auto& sub : it->second) {
                sub.callback(event);
            }
        }
    }

    void off(SubscriptionId id) override {
        for (auto& pair : callback_subscriptions_) {
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
        callback_subscriptions_.clear();
        next_id_ = 1;
    }

    size_t getSubscriberCount() const {
        size_t count = 0;
        for (const auto& pair : callback_subscriptions_) {
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

    SubscriptionMap callback_subscriptions_;
    SubscriptionId next_id_;
};
