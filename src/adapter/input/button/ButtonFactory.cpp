#include "ButtonFactory.hpp"

#include "../../multiplexer/MultiplexerController.hpp"
#include "log/Macros.hpp"
#include "reader/TeensyMultiplexerPinReader.hpp"
#include "reader/TeensyPinReader.hpp"

std::unique_ptr<UnifiedButton> ButtonFactory::createButton(const Hardware::Button& setup,
                                                           Multiplexer& mux) {
    auto pinReader = createPinReader(setup.pin, mux);

    if (!pinReader) {
        LOGLN("[ButtonFactory] ERROR: Failed to create pin reader for button");
        return nullptr;
    }

    return std::make_unique<UnifiedButton>(setup, std::move(pinReader));
}

std::unique_ptr<IPinReader> ButtonFactory::createPinReader(const GpioPin& gpio, Multiplexer& mux) {
    switch (gpio.source) {
    case GpioPin::Source::MCU:
        if (gpio.pin > 41) {
            LOGLN("[ButtonFactory] ERROR: Invalid MCU pin");
            return nullptr;
        }
        return std::make_unique<TeensyPinReader>(gpio.pin, gpio.mode);

    case GpioPin::Source::MUX:
        if (gpio.pin > 15) {
            LOGLN("[ButtonFactory] ERROR: Invalid MUX channel");
            return nullptr;
        }
        return std::make_unique<TeensyMultiplexerPinReader>(gpio.pin, mux);

    default:
        LOGLN("[ButtonFactory] ERROR: Unsupported GPIO source");
        return nullptr;
    }
}