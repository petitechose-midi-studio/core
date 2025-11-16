#pragma once

#include <memory>

#include "core/struct/Button.hpp"
#include "reader/IPinReader.hpp"

class UnifiedButton {
public:
    UnifiedButton(const Hardware::Button& setup, std::unique_ptr<IPinReader> pinReader);

    void update();
    bool isPressed() const;
    ButtonID getId() const;

private:
    Hardware::Button button_;
    std::unique_ptr<IPinReader> pinReader_;

    bool pressed_;
    bool lastState_;

    bool readCurrentState();
};