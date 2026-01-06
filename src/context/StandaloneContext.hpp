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
#include "ui/OverlayController.hpp"
#include "ui/ViewContainer.hpp"
#include "ui/macro/MacroEditOverlay.hpp"
#include "ui/font/CoreFonts.hpp"
#include "ui/font/StandaloneFonts.hpp"
#include "ui/transportbar/TransportBar.hpp"
#include "ui/view/MacroView.hpp"

namespace core::context {

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
    explicit StandaloneContext(core::state::CoreState& state) : core_state_(state) {}

    bool initialize() override {
        OC_LOG_INFO("StandaloneContext::initialize()");

        oc::ui::lvgl::font::load(CORE_FONT_ENTRIES, CORE_FONT_COUNT);
        oc::ui::lvgl::font::load(STANDALONE_FONT_ENTRIES, STANDALONE_FONT_COUNT);
        linkCoreFontAliases();

        // Sync encoder positions with restored values BEFORE creating handlers
        syncEncodersFromState();

        // Create UI container with zones
        view_container_ = std::make_unique<core::ui::ViewContainer>(lv_screen_active());

        // Create MacroView in main zone (TopBar is now internal to MacroView)
        view_ = std::make_unique<core::ui::MacroView>(
            view_container_->getMainZone(),
            core_state_
        );
        view_->onActivate();

        // Create TransportBar in bottom zone
        transport_bar_ = std::make_unique<core::ui::TransportBar>(
            view_container_->getBottomZone(),
            core_state_.statusBar
        );

        // Create overlay controller with AuthorityResolver
        overlay_controller_ = std::make_unique<core::ui::OverlayController<core::ui::OverlayType>>(
            core_state_.overlays, buttons()
        );
        buttons().setAuthorityResolver(&overlay_controller_->authority());

        // Create MacroEdit overlay (stateless - no state reference)
        macro_edit_overlay_ = std::make_unique<core::ui::MacroEditOverlay>(lv_screen_active());

        // Register overlay cleanup
        overlay_controller_->registerCleanup(
            core::ui::OverlayType::MACRO_EDIT,
            reinterpret_cast<oc::core::ScopeID>(macro_edit_overlay_->getElement()),
            static_cast<oc::hal::ButtonID>(0)  // No latch button
        );

        // Setup rendering subscriptions for MacroEditOverlay (orchestrator pattern)
        setupMacroEditRendering();

        // Create handlers (bindings scoped to view element)
        input_handler_ = std::make_unique<core::handler::HandlerInputMacro>(
            core_state_, encoders(), midi(), view_->getElement()
        );
        midi_handler_ = std::make_unique<core::handler::HandlerInputMacroMidi>(
            core_state_, midi(), encoders()
        );
        transport_handler_ = std::make_unique<core::handler::HandlerInputTransport>(
            core_state_, encoders(), buttons(), view_->getElement()
        );

        // Create MacroEdit input handler (two-level scoping)
        macro_edit_handler_ = std::make_unique<core::handler::HandlerInputMacroEdit>(
            core_state_,
            *overlay_controller_,
            encoders(),
            buttons(),
            view_->getElement(),              // MacroView scope (open trigger)
            macro_edit_overlay_->getElement()   // Overlay scope (edit/close)
        );

        view_container_->show();

        OC_LOG_INFO("StandaloneContext ready");
        return true;
    }

    void syncEncodersFromState() {
        for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
            float value = core_state_.macros.slots[i].value.get();
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

        macro_edit_subs_.push_back(core_state_.macroEdit.visible.subscribe(
            [render](bool) { render(); }
        ));
        macro_edit_subs_.push_back(core_state_.macroEdit.editingIndex.subscribe(
            [render](uint8_t) { render(); }
        ));
        macro_edit_subs_.push_back(core_state_.macroEdit.tempChannel.subscribe(
            [render](uint8_t) { render(); }
        ));
        macro_edit_subs_.push_back(core_state_.macroEdit.tempCC.subscribe(
            [render](uint8_t) { render(); }
        ));
        macro_edit_subs_.push_back(core_state_.macroEdit.focusedRow.subscribe(
            [render](uint8_t) { render(); }
        ));
    }

    /**
     * @brief Render MacroEditOverlay with current state
     */
    void renderMacroEdit() {
        macro_edit_overlay_->render({
            .editingIndex = core_state_.macroEdit.editingIndex.get(),
            .channel = core_state_.macroEdit.tempChannel.get(),
            .cc = core_state_.macroEdit.tempCC.get(),
            .focusedRow = core_state_.macroEdit.focusedRow.get(),
            .visible = core_state_.macroEdit.visible.get()
        });
    }

    void update() override {}

    void cleanup() override {
        // Handlers first (they reference state/APIs)
        macro_edit_handler_.reset();
        transport_handler_.reset();
        input_handler_.reset();
        midi_handler_.reset();

        // Clear MacroEdit rendering subscriptions
        macro_edit_subs_.clear();

        // Overlay UI
        macro_edit_overlay_.reset();

        // Overlay controller (clears authority resolver)
        overlay_controller_.reset();

        // TransportBar (TopBar is now managed by MacroView)
        transport_bar_.reset();

        if (view_) {
            view_->onDeactivate();
            view_.reset();
        }

        view_container_.reset();
    }

    const char* getName() const override { return "Standalone"; }

private:
    core::state::CoreState& core_state_;  // External reference (survives context switches)

    // UI containers
    std::unique_ptr<core::ui::ViewContainer> view_container_;
    std::unique_ptr<core::ui::MacroView> view_;  // MacroView owns its TopBar internally
    std::unique_ptr<core::ui::TransportBar> transport_bar_;

    // Overlay system
    std::unique_ptr<core::ui::OverlayController<core::ui::OverlayType>> overlay_controller_;
    std::unique_ptr<core::ui::MacroEditOverlay> macro_edit_overlay_;
    std::vector<oc::state::Subscription> macro_edit_subs_;  ///< MacroEdit rendering subscriptions

    // Handlers
    std::unique_ptr<core::handler::HandlerInputMacro> input_handler_;
    std::unique_ptr<core::handler::HandlerInputMacroMidi> midi_handler_;
    std::unique_ptr<core::handler::HandlerInputTransport> transport_handler_;
    std::unique_ptr<core::handler::HandlerInputMacroEdit> macro_edit_handler_;
};

}  // namespace core::context
