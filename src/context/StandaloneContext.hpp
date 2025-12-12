#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main application context for standalone operation
 *
 * Displays 8 macro knobs that send MIDI CC when turned.
 * Uses reactive state for UI updates.
 */

#include <array>
#include <memory>

#include <lvgl.h>

#include <oc/context/IContext.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/log/Log.hpp>

#include "config/App.hpp"
#include "state/MacroState.hpp"
#include "ui/font/FontLoader.hpp"
#include "ui/view/MacroView.hpp"

namespace context {

/**
 * @brief Standalone mode context
 *
 * Main application mode when not connected to a DAW.
 * Displays 8 macro knobs bound to encoders, sending MIDI CC.
 */
class StandaloneContext : public oc::context::IContext {
public:
    static constexpr oc::context::Requirements REQUIRES{
        .button = true,
        .encoder = true,
        .midi = true
    };

    bool initialize() override {
        // Load remaining fonts (non-essential ones)
        loadPluginFonts();

        // Create macro view bound to state
        view_ = std::make_unique<MacroView>(lv_screen_active(), state_);
        view_->onActivate();

        setupInputBindings();
        return true;
    }

    void update() override {}

    void cleanup() override {
        if (view_) {
            view_->onDeactivate();
            view_.reset();
        }
    }

    const char* getName() const override { return "Standalone"; }

private:
    state::MacroState state_;
    std::unique_ptr<MacroView> view_;

    void setupInputBindings() {
        using EncID = Config::EncoderID;

        // Encoder IDs for macros 1-8
        static constexpr std::array<EncID, 8> MACRO_ENCODERS = {
            EncID::MACRO_1, EncID::MACRO_2, EncID::MACRO_3, EncID::MACRO_4,
            EncID::MACRO_5, EncID::MACRO_6, EncID::MACRO_7, EncID::MACRO_8
        };

        // MIDI CC numbers for each macro (CC 1-8)
        static constexpr std::array<uint8_t, 8> MIDI_CC = {1, 2, 3, 4, 5, 6, 7, 8};

        // Bind each encoder to state (UI updates automatically via subscription)
        for (uint8_t i = 0; i < 8; ++i) {
            onEncoder(MACRO_ENCODERS[i]).turn().then([this, i](float value) {
                // Update state (triggers UI update via signal)
                state_.values[i].set(value);

                // Send MIDI CC
                uint8_t cc_value = static_cast<uint8_t>(value * 127);
                midi().sendCC(0, MIDI_CC[i], cc_value);
            });
        }
    }
};

}  // namespace context
