#include "ui/ViewController.hpp"

#include "core/event/UnifiedEventTypes.hpp"
#include "manager/ViewManager.hpp"

ViewController::ViewController(ViewManager& viewManager, IEventBus& eventBus)
    : view_manager_(viewManager), event_bus_(eventBus) {
    // ViewController is kept for future Core view navigation
    // Currently, Core only has splash screen, plugins manage their own views

    boot_complete_sub_ = event_bus_.on(EventCategory::SYSTEM, SystemEvent::BOOT_COMPLETE,
                                       [this](const Event& e) { onSystemBootComplete(e); });
}

void ViewController::onSystemBootComplete(const Event& event) {
    // Reserved for future Core view initialization after boot
}
