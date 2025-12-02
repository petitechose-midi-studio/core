/*
 * InputFactory.hpp
 *
 * Factory to load input configurations from config/ into runtime containers.
 * Converts compile-time constexpr arrays into std::vector for use by controllers.
 */

#pragma once

#include <vector>

#include "config/InputDefinition.hpp"
#include "struct/Button.hpp"
#include "struct/Encoder.hpp"

class InputFactory {
public:
    /*
     * Load encoder configurations
     */
    static std::vector<Hardware::Encoder> createEncoders() {
        std::vector<Hardware::Encoder> encoders;
        encoders.reserve(std::size(Config::ENCODERS));
        for (const auto& enc : Config::ENCODERS) { encoders.push_back(enc); }
        return encoders;
    }

    /*
     * Load button configurations
     */
    static std::vector<Hardware::Button> createButtons() {
        std::vector<Hardware::Button> buttons;
        buttons.reserve(std::size(Config::BUTTONS));
        for (const auto& btn : Config::BUTTONS) { buttons.push_back(btn); }
        return buttons;
    }
};
