#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main application context for standalone operation
 *
 * Provides 8 macro knobs with MIDI CC output and bidirectional sync.
 * Receives CoreState reference from main.cpp (state survives context switches).
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
#include "ui/topbar/TopBar.hpp"
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

        // Create TopBar in top zone (Props pattern)
        topBar_ = std::make_unique<ui::TopBar>(viewContainer_->getTopZone());
        setupTopBarRendering();
        renderTopBar();  // Initial render

        // Create MacroView in main zone
        view_ = std::make_unique<MacroView>(
            viewContainer_->getMainZone(),
            coreState_
        );
        view_->onActivate();

        // Create TransportBar in bottom zone (Props pattern)
        transportBar_ = std::make_unique<ui::TransportBar>(viewContainer_->getBottomZone());
        setupTransportBarRendering();
        renderTransportBar();  // Initial render

        // Create overlay controller with AuthorityResolver
        overlayController_ = std::make_unique<state::OverlayController>(
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

        // Setup rendering subscriptions (orchestrator pattern)
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

    /**
     * @brief Setup subscriptions to render TopBar from state
     */
    void setupTopBarRendering() {
        topBarSubs_.push_back(coreState_.statusBar.pageName.subscribe(
            [this](const char*) { renderTopBar(); }
        ));
    }

    /**
     * @brief Render TopBar with current state
     */
    void renderTopBar() {
        topBar_->render({
            .pageName = coreState_.statusBar.pageName.get()
        });
    }

    /**
     * @brief Setup subscriptions to render TransportBar from state
     */
    void setupTransportBarRendering() {
        auto render = [this]() { renderTransportBar(); };

        transportBarSubs_.push_back(coreState_.statusBar.noteInActive.subscribe(
            [render](bool) { render(); }
        ));
        transportBarSubs_.push_back(coreState_.statusBar.noteOutActive.subscribe(
            [render](bool) { render(); }
        ));
        transportBarSubs_.push_back(coreState_.statusBar.ccInActive.subscribe(
            [render](bool) { render(); }
        ));
        transportBarSubs_.push_back(coreState_.statusBar.ccOutActive.subscribe(
            [render](bool) { render(); }
        ));
        transportBarSubs_.push_back(coreState_.statusBar.playing.subscribe(
            [render](bool) { render(); }
        ));
        transportBarSubs_.push_back(coreState_.statusBar.tempo.subscribe(
            [render](float) { render(); }
        ));
        transportBarSubs_.push_back(coreState_.statusBar.beatPulse.subscribe(
            [render](bool) { render(); }
        ));
    }

    /**
     * @brief Render TransportBar with current state
     */
    void renderTransportBar() {
        transportBar_->render({
            .noteInActive = coreState_.statusBar.noteInActive.get(),
            .noteOutActive = coreState_.statusBar.noteOutActive.get(),
            .ccInActive = coreState_.statusBar.ccInActive.get(),
            .ccOutActive = coreState_.statusBar.ccOutActive.get(),
            .playing = coreState_.statusBar.playing.get(),
            .tempo = coreState_.statusBar.tempo.get(),
            .beatPulse = coreState_.statusBar.beatPulse.get(),
            // Pulse completion callbacks - reset state signals
            .onNoteInPulseComplete = [this]() {
                coreState_.statusBar.noteInActive.set(false);
            },
            .onNoteOutPulseComplete = [this]() {
                coreState_.statusBar.noteOutActive.set(false);
            },
            .onCcInPulseComplete = [this]() {
                coreState_.statusBar.ccInActive.set(false);
            },
            .onCcOutPulseComplete = [this]() {
                coreState_.statusBar.ccOutActive.set(false);
            },
            .onBeatPulseComplete = [this]() {
                coreState_.statusBar.beatPulse.set(false);
            }
        });
    }

    void update() override {}

    void cleanup() override {
        // Handlers first (they reference state/APIs)
        macroEditHandler_.reset();
        transportHandler_.reset();
        inputHandler_.reset();
        midiHandler_.reset();

        // Clear all rendering subscriptions
        macroEditSubs_.clear();
        topBarSubs_.clear();
        transportBarSubs_.clear();

        // Overlay UI
        macroEditOverlay_.reset();

        // Overlay controller (clears authority resolver)
        overlayController_.reset();

        // Bars
        topBar_.reset();
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
    std::unique_ptr<ui::TopBar> topBar_;
    std::unique_ptr<MacroView> view_;
    std::unique_ptr<ui::TransportBar> transportBar_;

    // Overlay system
    std::unique_ptr<state::OverlayController> overlayController_;
    std::unique_ptr<ui::MacroEditOverlay> macroEditOverlay_;

    // Rendering subscriptions (orchestrator pattern)
    std::vector<oc::state::Subscription> macroEditSubs_;
    std::vector<oc::state::Subscription> topBarSubs_;
    std::vector<oc::state::Subscription> transportBarSubs_;

    // Handlers
    std::unique_ptr<handler::HandlerInputMacro> inputHandler_;
    std::unique_ptr<handler::HandlerInputMacroMidi> midiHandler_;
    std::unique_ptr<handler::HandlerInputTransport> transportHandler_;
    std::unique_ptr<handler::HandlerInputMacroEdit> macroEditHandler_;
};

}  // namespace context
