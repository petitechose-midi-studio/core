#pragma once

#include <oc/state/NotificationQueue.hpp>

namespace test_support {

inline void drainNotifications() {
    auto& queue = oc::state::NotificationQueue::instance();
    while (queue.hasPending()) {
        queue.flush();
    }
}

}  // namespace test_support
