#include "ButtonController.hpp"

#include <Arduino.h>

#include "../../multiplexer/MultiplexerController.hpp"
#include "ButtonFactory.hpp"
#include "config/System.hpp"
#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"
#include "log/Macros.hpp"

ButtonController::ButtonController(
    const etl::vector<Hardware::Button, System::Hardware::BUTTONS_COUNT>& buttonSetups,
    Multiplexer& mux, IEventBus& eventBus)
    : eventBus_(eventBus) {
    for (const auto& setup : buttonSetups) {
        auto button = ButtonFactory::createButton(setup, mux);

        if (button) {
            size_t index = ownedButtons_.size();
            ownedButtons_.push_back(std::move(button));
            lastStates_.push_back(false);
            lastChangeTime_.push_back(0);  // Initialize debounce timer
            idToIndex_[setup.id] = index;  // Map InputId -> array index
        } else {
            LOGLN("[ButtonController] ERROR: Failed to create button");
        }
    }
}

ButtonController::~ButtonController() = default;

void ButtonController::updateAll() {
    uint32_t now = millis();

    for (size_t i = 0; i < ownedButtons_.size(); ++i) {
        auto& btn = ownedButtons_[i];
        btn->update();

        bool currentState = btn->isPressed();
        if (currentState == lastStates_[i]) {
            continue;
        }

        uint32_t elapsed = now - lastChangeTime_[i];
        if (elapsed < System::Input::BUTTON_DEBOUNCE_MS) {
            continue;  // Too soon - ignore this change
        }

        lastStates_[i] = currentState;
        lastChangeTime_[i] = now;

        if (currentState) {
            eventBus_.emit(ButtonPressEvent(btn->getId(), true));
        } else {
            eventBus_.emit(ButtonReleaseEvent(btn->getId()));
        }
    }
}

UnifiedButton* ButtonController::getButton(ButtonID id) {
    auto it = idToIndex_.find(id);
    return (it != idToIndex_.end()) ? ownedButtons_[it->second].get() : nullptr;
}

const UnifiedButton* ButtonController::getButton(ButtonID id) const {
    auto it = idToIndex_.find(id);
    return (it != idToIndex_.end()) ? ownedButtons_[it->second].get() : nullptr;
}