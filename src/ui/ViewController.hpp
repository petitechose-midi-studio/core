#pragma once

#include "core/event/IEventBus.hpp"

class ViewManager;
class Event;

/**
 * ViewController - Core view orchestrator (legacy placeholder)
 *
 * Currently minimal as Core only has splash screen.
 * Plugins manage their own views independently.
 *
 * Reserved for future Core view navigation (menus, settings, etc).
 */
class ViewController {
public:
    ViewController(ViewManager& viewManager, IEventBus& eventBus);
    ~ViewController() = default;

private:
    ViewManager& viewManager_;
    IEventBus& eventBus_;

    SubscriptionId bootCompleteSub_ = 0;

    void onSystemBootComplete(const Event& event);
};
