#include "UnifiedButton.hpp"

#include "log/Macros.hpp"

UnifiedButton::UnifiedButton(const Hardware::Button& setup, std::unique_ptr<IPinReader> pinReader)
    : button_(setup), pin_reader_(std::move(pinReader)), pressed_(false), last_state_(false) {
    if (!pin_reader_) {
        LOGLN("[UnifiedButton] ERROR: Null pinReader for button");
        return;
    }

    pin_reader_->initialize();
    bool initialState = readCurrentState();
    last_state_ = initialState;
}

void UnifiedButton::update() {
    if (!pin_reader_) {
        return;
    }

    pin_reader_->update();
    bool currentState = readCurrentState();
    last_state_ = currentState;
    pressed_ = currentState;
}

bool UnifiedButton::isPressed() const {
    return pressed_;
}

ButtonID UnifiedButton::getId() const {
    return button_.id;
}

bool UnifiedButton::readCurrentState() {
    bool rawValue = pin_reader_->read();
    return !rawValue;
}