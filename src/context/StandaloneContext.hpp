#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main application context for standalone operation
 */

#include <memory>

#include <lvgl.h>

#include <oc/context/IContext.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/log/Log.hpp>

#include "config/App.hpp"
#include "ui/view/EmptyView.hpp"

namespace context {

/**
 * @brief Standalone mode context
 *
 * Main application mode when not connected to a DAW.
 */
class StandaloneContext : public oc::context::IContext {
public:
    static constexpr oc::context::Requirements REQUIRES{
        .button = true,
        .encoder = true,
        .midi = true
    };

    bool initialize() override {
        // Default empty view (black background)
        view_ = std::make_unique<EmptyView>(lv_screen_active());
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
    std::unique_ptr<EmptyView> view_;

    void setupInputBindings() {
        using EncID = Config::EncoderID;

        onEncoder(EncID::MACRO_1).turn().then([this](float value) {
            uint8_t cc_value = static_cast<uint8_t>(value * 127);
            midi().sendCC(1, 1, cc_value);
            OC_LOG_DEBUG("MIDI CC1: {}", cc_value);
        });
    }
};

}  // namespace context
