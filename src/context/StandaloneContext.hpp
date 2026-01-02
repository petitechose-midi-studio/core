#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main application context for standalone operation
 *
 * Provides 8 macro knobs with MIDI CC output and bidirectional sync.
 * Receives CoreState reference from main.cpp (state survives context switches).
 */

#include <memory>

#include <lvgl.h>

#include <oc/context/IContext.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/log/Log.hpp>

#include "config/App.hpp"
#include "handler/MacroInputHandler.hpp"
#include "handler/MacroMidiHandler.hpp"
#include "state/CoreState.hpp"
#include "ui/ViewContainer.hpp"
#include "ui/font/FontLoader.hpp"
#include "ui/view/MacroView.hpp"

namespace context {

/**
 * @brief Standalone mode context
 *
 * 8 macro knobs with:
 * - Encoder → MIDI CC out
 * - MIDI CC in → state + encoder sync
 *
 * Receives CoreState reference (state survives context switches).
 */
class StandaloneContext : public oc::context::IContext {
public:
    static constexpr oc::context::Requirements REQUIRES{
        .button = true,
        .encoder = true,
        .midi = true
    };

    /**
     * @brief Construct with external CoreState reference
     * @param state Reference to global CoreState (owned by main.cpp)
     */
    explicit StandaloneContext(state::CoreState& state) : coreState_(state) {}

    bool initialize() override {
        OC_LOG_INFO("StandaloneContext::initialize()");

        fontsRegisterCore();
        loadPluginFonts();

        // Create UI container with zones
        viewContainer_ = std::make_unique<ui::ViewContainer>(lv_screen_active());

        // Create MacroView in main zone (not directly on screen)
        view_ = std::make_unique<MacroView>(
            viewContainer_->getMainZone(),
            coreState_.macros
        );
        view_->onActivate();

        // Create handlers (bindings scoped to view element)
        inputHandler_ = std::make_unique<handler::MacroInputHandler>(
            coreState_, encoders(), midi(), view_->getElement()
        );
        midiHandler_ = std::make_unique<handler::MacroMidiHandler>(
            coreState_, midi(), encoders()
        );

        viewContainer_->show();

        OC_LOG_INFO("StandaloneContext ready");
        return true;
    }

    void update() override {}

    void cleanup() override {
        // Handlers first (they reference state/APIs)
        inputHandler_.reset();
        midiHandler_.reset();

        if (view_) {
            view_->onDeactivate();
            view_.reset();
        }

        viewContainer_.reset();
    }

    const char* getName() const override { return "Standalone"; }

private:
    state::CoreState& coreState_;  // External reference (survives context switches)
    std::unique_ptr<ui::ViewContainer> viewContainer_;
    std::unique_ptr<MacroView> view_;
    std::unique_ptr<handler::MacroInputHandler> inputHandler_;
    std::unique_ptr<handler::MacroMidiHandler> midiHandler_;
};

}  // namespace context
