/*
 * InputFactory.hpp
 *
 * Factory to load input configurations from config/ into runtime containers.
 * Converts compile-time constexpr arrays into etl::vector for use by controllers.
 */

#pragma once

#include <etl/vector.h>

#include "../struct/Button.hpp"
#include "../struct/Encoder.hpp"
#include "config/InputDefinition.hpp"
#include "config/System.hpp"

class InputFactory {
public:
    /*
     * Load encoder configurations
     */
    static etl::vector<Hardware::Encoder, System::Hardware::ENCODERS_COUNT> createEncoders() {
        etl::vector<Hardware::Encoder, System::Hardware::ENCODERS_COUNT> encoders;
        for (const auto& enc : Config::ENCODERS) {
            encoders.push_back(enc);
        }
        return encoders;
    }

    /*
     * Load button configurations
     */
    static etl::vector<Hardware::Button, System::Hardware::BUTTONS_COUNT> createButtons() {
        etl::vector<Hardware::Button, System::Hardware::BUTTONS_COUNT> buttons;
        for (const auto& btn : Config::BUTTONS) {
            buttons.push_back(btn);
        }
        return buttons;
    }
};
