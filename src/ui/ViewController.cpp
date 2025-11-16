#include "ui/ViewController.hpp"

#include "core/event/Events.hpp"
#include "core/event/UnifiedEventTypes.hpp"
#include "manager/ViewManager.hpp"

using namespace EventCategory;
using namespace SystemEvent;

ViewController::ViewController(ViewManager& viewManager, IEventBus& eventBus)
    : viewManager_(viewManager), eventBus_(eventBus) {
    // ViewController is kept for future Core view navigation
    // Currently, Core only has splash screen, plugins manage their own views

    bootCompleteSub_ = eventBus_.on(EventCategory::System, BootComplete, [this](const Event& e) {
        onSystemBootComplete(e);
    });
}

void ViewController::onSystemBootComplete(const Event& event) {
    // Reserved for future Core view initialization after boot
}
