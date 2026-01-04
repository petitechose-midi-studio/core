#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main context for standalone operation mode
 *
 * StandaloneContext manages the lifecycle and wires together:
 * - CoreState: reactive state for all application data
 * - Handlers: input bindings for encoders/buttons
 * - Views: UI components that subscribe to state
 *
 * ## Architecture
 *
 * ```
 * StandaloneContext
 *     ├── CoreState (reactive state - single source of truth, external)
 *     ├── ViewContainer (2-zone layout: main + bottom)
 *     ├── InputHandlers
 *     │   ├── HandlerInputMacro (encoder → MIDI CC out)
 *     │   ├── HandlerInputMacroMidi (MIDI CC in → state)
 *     │   ├── HandlerInputMacroEdit (overlay editing)
 *     │   └── HandlerInputTransport (transport controls)
 *     ├── Views
 *     │   ├── MacroView (main zone, owns TopBar)
 *     │   └── TransportBar (bottom zone)
 *     └── Overlays (managed by OverlayController)
 *         └── MacroEditOverlay (edit CH/CC for a macro)
 * ```
 *
 * The context itself is thin - handlers and views do the work.
 * CoreState is received from main.cpp (survives context switches).
 */

#include <array>
#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/context/IContext.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/log/Log.hpp>

#include <oc/ui/lvgl/FontLoader.hpp>

#include "config/App.hpp"
#include "handler/input/HandlerInputMacro.hpp"
#include "handler/input/HandlerInputMacroEdit.hpp"
#include "handler/input/HandlerInputMacroMidi.hpp"
#include "handler/input/HandlerInputTransport.hpp"
#include "state/CoreState.hpp"
#include "state/OverlayController.hpp"
#include "ui/ViewContainer.hpp"
#include "ui/macro/MacroEditOverlay.hpp"
#include "ui/font/CoreFonts.hpp"
#include "ui/font/StandaloneFonts.hpp"
#include "ui/transportbar/TransportBar.hpp"
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

        oc::ui::lvgl::font::load(CORE_FONT_ENTRIES, CORE_FONT_COUNT);
        oc::ui::lvgl::font::load(STANDALONE_FONT_ENTRIES, STANDALONE_FONT_COUNT);
        linkCoreFontAliases();

        // Sync encoder positions with restored values BEFORE creating handlers
        syncEncodersFromState();

        // Create UI container with zones
        viewContainer_ = std::make_unique<ui::ViewContainer>(lv_screen_active());

        // Create MacroView in main zone (TopBar is now internal to MacroView)
        view_ = std::make_unique<MacroView>(
            viewContainer_->getMainZone(),
            coreState_
        );
        view_->onActivate();

        // Create TransportBar in bottom zone
        transportBar_ = std::make_unique<ui::TransportBar>(
            viewContainer_->getBottomZone(),
            coreState_.statusBar
        );

        // Create overlay controller with AuthorityResolver
        overlayController_ = std::make_unique<state::OverlayController<state::CoreOverlayType>>(
            coreState_.overlays, buttons()
        );
        buttons().setAuthorityResolver(&overlayController_->authority());

        // Create MacroEdit overlay (stateless - no state reference)
        macroEditOverlay_ = std::make_unique<ui::MacroEditOverlay>(lv_screen_active());

        // Register overlay cleanup
        overlayController_->registerCleanup(
            state::CoreOverlayType::MACRO_EDIT,
            reinterpret_cast<oc::core::ScopeID>(macroEditOverlay_->getElement()),
            static_cast<oc::hal::ButtonID>(0)  // No latch button
        );

        // Setup rendering subscriptions for MacroEditOverlay (orchestrator pattern)
        setupMacroEditRendering();

        // Create handlers (bindings scoped to view element)
        inputHandler_ = std::make_unique<handler::HandlerInputMacro>(
            coreState_, encoders(), midi(), view_->getElement()
        );
        midiHandler_ = std::make_unique<handler::HandlerInputMacroMidi>(
            coreState_, midi(), encoders()
        );
        transportHandler_ = std::make_unique<handler::HandlerInputTransport>(
            coreState_, encoders(), buttons(), view_->getElement()
        );

        // Create MacroEdit input handler (two-level scoping)
        macroEditHandler_ = std::make_unique<handler::HandlerInputMacroEdit>(
            coreState_,
            *overlayController_,
            encoders(),
            buttons(),
            view_->getElement(),              // MacroView scope (open trigger)
            macroEditOverlay_->getElement()   // Overlay scope (edit/close)
        );

        viewContainer_->show();

        OC_LOG_INFO("StandaloneContext ready");
        return true;
    }

    void syncEncodersFromState() {
        for (uint8_t i = 0; i < state::MACRO_COUNT; ++i) {
            float value = coreState_.macros.slots[i].value.get();
            encoders().setPosition(Config::MACRO_ENCODERS[i], value);
        }
        OC_LOG_DEBUG("Synced encoder positions from restored state");
    }

    /**
     * @brief Setup subscriptions to render MacroEditOverlay from state
     *
     * Orchestrator pattern: Context subscribes to state signals and
     * calls overlay->render(props) when state changes.
     */
    void setupMacroEditRendering() {
        auto render = [this]() { renderMacroEdit(); };

        macroEditSubs_.push_back(coreState_.macroEdit.visible.subscribe(
            [render](bool) { render(); }
        ));
        macroEditSubs_.push_back(coreState_.macroEdit.editingIndex.subscribe(
            [render](uint8_t) { render(); }
        ));
        macroEditSubs_.push_back(coreState_.macroEdit.tempChannel.subscribe(
            [render](uint8_t) { render(); }
        ));
        macroEditSubs_.push_back(coreState_.macroEdit.tempCC.subscribe(
            [render](uint8_t) { render(); }
        ));
        macroEditSubs_.push_back(coreState_.macroEdit.focusedRow.subscribe(
            [render](uint8_t) { render(); }
        ));
    }

    /**
     * @brief Render MacroEditOverlay with current state
     */
    void renderMacroEdit() {
        macroEditOverlay_->render({
            .editingIndex = coreState_.macroEdit.editingIndex.get(),
            .channel = coreState_.macroEdit.tempChannel.get(),
            .cc = coreState_.macroEdit.tempCC.get(),
            .focusedRow = coreState_.macroEdit.focusedRow.get(),
            .visible = coreState_.macroEdit.visible.get()
        });
    }

    void update() override {}

    void cleanup() override {
        // Handlers first (they reference state/APIs)
        macroEditHandler_.reset();
        transportHandler_.reset();
        inputHandler_.reset();
        midiHandler_.reset();

        // Clear MacroEdit rendering subscriptions
        macroEditSubs_.clear();

        // Overlay UI
        macroEditOverlay_.reset();

        // Overlay controller (clears authority resolver)
        overlayController_.reset();

        // TransportBar (TopBar is now managed by MacroView)
        transportBar_.reset();

        if (view_) {
            view_->onDeactivate();
            view_.reset();
        }

        viewContainer_.reset();
    }

    const char* getName() const override { return "Standalone"; }

private:
    state::CoreState& coreState_;  // External reference (survives context switches)

    // UI containers
    std::unique_ptr<ui::ViewContainer> viewContainer_;
    std::unique_ptr<MacroView> view_;  // MacroView owns its TopBar internally
    std::unique_ptr<ui::TransportBar> transportBar_;

    // Overlay system
    std::unique_ptr<state::OverlayController<state::CoreOverlayType>> overlayController_;
    std::unique_ptr<ui::MacroEditOverlay> macroEditOverlay_;
    std::vector<oc::state::Subscription> macroEditSubs_;  ///< MacroEdit rendering subscriptions

    // Handlers
    std::unique_ptr<handler::HandlerInputMacro> inputHandler_;
    std::unique_ptr<handler::HandlerInputMacroMidi> midiHandler_;
    std::unique_ptr<handler::HandlerInputTransport> transportHandler_;
    std::unique_ptr<handler::HandlerInputMacroEdit> macroEditHandler_;
};

}  // namespace context
