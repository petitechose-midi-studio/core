#include "UnifiedButton.hpp"

#include "log/Macros.hpp"

UnifiedButton::UnifiedButton(const Hardware::Button& setup, std::unique_ptr<IPinReader> pinReader)
    : button_(setup), pinReader_(std::move(pinReader)), pressed_(false), lastState_(false) {
    if (!pinReader_) {
        LOGLN("[UnifiedButton] ERROR: Null pinReader for button");
        return;
    }

    pinReader_->initialize();
    bool initialState = readCurrentState();
    lastState_ = initialState;
}

void UnifiedButton::update() {
    if (!pinReader_) {
        return;
    }

    pinReader_->update();
    bool currentState = readCurrentState();
    lastState_ = currentState;
    pressed_ = currentState;
}

bool UnifiedButton::isPressed() const {
    return pressed_;
}

ButtonID UnifiedButton::getId() const {
    return button_.id;
}

bool UnifiedButton::readCurrentState() {
    bool rawValue = pinReader_->read();
    return !rawValue;
}