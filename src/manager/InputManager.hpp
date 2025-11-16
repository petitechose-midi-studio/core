/*
 * InputManager.hpp
 *
 * Manages input controller lifecycle (setup and update).
 * Does not own controllers - they are passed by reference.
 */

#pragma once

class ButtonController;
class EncoderController;

class InputManager {
public:
    InputManager(EncoderController& encoders, ButtonController& buttons)
        : encoders_(encoders), buttons_(buttons) {}

    void update();

private:
    EncoderController& encoders_;
    ButtonController& buttons_;
};
