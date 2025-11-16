#pragma once

#include <memory>

#include "UnifiedButton.hpp"
#include "core/struct/Button.hpp"
#include "reader/IPinReader.hpp"

class Multiplexer;

class ButtonFactory {
public:
    static std::unique_ptr<UnifiedButton> createButton(const Hardware::Button& setup,
                                                       Multiplexer& mux);

private:
    static std::unique_ptr<IPinReader> createPinReader(const GpioPin& gpio, Multiplexer& mux);
};