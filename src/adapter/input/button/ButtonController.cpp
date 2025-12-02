#include "ButtonController.hpp"

#include "ButtonFactory.hpp"

#include <Arduino.h>

#include "adapter/multiplexer/MultiplexerController.hpp"
#include "config/System.hpp"
#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"
#include "log/Macros.hpp"

ButtonController::ButtonController(const std::vector<Hardware::Button>& buttonSetups,
                                   Multiplexer& mux, IEventBus& eventBus)
    : event_bus_(eventBus) {
    owned_buttons_.reserve(buttonSetups.size());
    last_states_.reserve(buttonSetups.size());
    last_change_time_.reserve(buttonSetups.size());

    for (const auto& setup : buttonSetups) {
        auto button = ButtonFactory::createButton(setup, mux);

        if (button) {
            size_t index = owned_buttons_.size();
            owned_buttons_.push_back(std::move(button));
            last_states_.push_back(false);
            last_change_time_.push_back(0);
            id_to_index_[setup.id] = index;
        } else {
            LOGLN("[ButtonController] ERROR: Failed to create button");
        }
    }
}

ButtonController::~ButtonController() = default;

void ButtonController::updateAll() {
    uint32_t now = millis();

    for (size_t i = 0; i < owned_buttons_.size(); ++i) {
        auto& btn = owned_buttons_[i];
        btn->update();

        bool currentState = btn->isPressed();
        if (currentState == last_states_[i]) { continue; }

        uint32_t elapsed = now - last_change_time_[i];
        if (elapsed < System::Input::BUTTON_DEBOUNCE_MS) {
            continue;  // Too soon - ignore this change
        }

        last_states_[i] = currentState;
        last_change_time_[i] = now;

        if (currentState) {
            event_bus_.emit(ButtonPressEvent(btn->getId(), true));
        } else {
            event_bus_.emit(ButtonReleaseEvent(btn->getId()));
        }
    }
}

UnifiedButton* ButtonController::getButton(ButtonID id) {
    auto it = id_to_index_.find(id);
    return (it != id_to_index_.end()) ? owned_buttons_[it->second].get() : nullptr;
}

const UnifiedButton* ButtonController::getButton(ButtonID id) const {
    auto it = id_to_index_.find(id);
    return (it != id_to_index_.end()) ? owned_buttons_[it->second].get() : nullptr;
}