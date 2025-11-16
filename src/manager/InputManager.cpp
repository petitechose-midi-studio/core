#include "InputManager.hpp"

#include "adapter/input/button/ButtonController.hpp"
#include "adapter/input/encoder/EncoderController.hpp"

/*
 * Update - Poll controllers
 */
void InputManager::update() {
    encoders_.flushAllEvents();
    buttons_.updateAll();
}
