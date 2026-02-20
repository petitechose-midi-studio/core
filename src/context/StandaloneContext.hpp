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
 *     ├── Handlers
 *     │   ├── MacroValueHandler (encoder → MIDI CC out)
 *     │   ├── MacroMidiHandler (MIDI CC in → state)
 *     │   ├── MacroEditHandler (overlay editing)
 *     │   └── TransportHandler (transport controls)
 *     ├── Views
 *     │   ├── MacroView (main zone, owns TopBar)
 *     │   └── TransportBar (bottom zone)
 *     └── Overlays (managed by OverlayManager)
 *         └── MacroEdit VirtualList overlays (property + selectors)
 * ```
 *
 * The context itself is thin - handlers and views do the work.
 * CoreState is received from main.cpp (survives context switches).
 */

#include <memory>
#include <array>

#include <oc/context/ContextBase.hpp>
#include <oc/context/Requirements.hpp>
#include <oc/state/SignalWatcher.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayTypes.hpp"

namespace ms::ui {
class ViewContainer;
class StringListSelector;
}  // namespace ms::ui

// Forward declarations
namespace core::handler {
class MacroValueHandler;
class MacroMidiHandler;
class MacroEditHandler;
class TransportHandler;
class ViewSwitcherHandler;
class SequencerStepHandler;
class SequencerPatternConfigHandler;
class SequencerStepEditHandler;
class SequencerPropertySelectorHandler;
class SequencerMacroPropertyHandler;
class GlobalSettingsHandler;
}  // namespace core::handler

namespace core::sequencer {
class SequencerPlaybackService;
class MidiClockSyncService;
}  // namespace core::sequencer

namespace core::ui {
class MacroView;
class SequencerView;
class TransportBar;
}  // namespace core::ui

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace oc::context {
template <typename T>
class OverlayManager;
}  // namespace oc::context

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
class StandaloneContext : public oc::context::ContextBase {
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
    explicit StandaloneContext(core::state::CoreState& state);

    ~StandaloneContext() override;

    // Non-copyable, non-movable
    StandaloneContext(const StandaloneContext&) = delete;
    StandaloneContext& operator=(const StandaloneContext&) = delete;
    StandaloneContext(StandaloneContext&&) = delete;
    StandaloneContext& operator=(StandaloneContext&&) = delete;

    // IContext interface
    oc::type::Result<void> init() override;
    void update() override;
    const char* getName() const override { return "Standalone"; }

protected:
    void onCleanup() override;

private:
    void syncEncodersFromState();
    void setupMacroEditRendering();
    void renderMacroEdit();
    void setupMacroEditSelectorRendering();
    void renderMacroEditSelector();
    void setupMacroPageSelectorRendering();
    void renderMacroPageSelector();
    void setupMacroTargetSelectorRendering();
    void renderMacroTargetSelector();
    void setupSequencerPatternConfigRendering();
    void renderSequencerPatternConfig();
    void setupSequencerStepEditRendering();
    void renderSequencerStepEdit();
    void setupSequencerPropertySelectorRendering();
    void renderSequencerPropertySelector();
    void setupSequencerMacroEncoderSync();
    void syncSequencerMacroEncoderPositions();
    void setupViewSelectorRendering();
    void renderViewSelector();
    void setupActiveViewSwitching();
    void applyActiveView();
    void setupGlobalSettingsRendering();
    void renderGlobalSettings();
    void setupGlobalSettingsSelectorRendering();
    void renderGlobalSettingsSelector();

    core::state::CoreState& core_state_;  // External reference (survives context switches)

    // UI containers
    std::unique_ptr<ms::ui::ViewContainer> view_container_;
    std::unique_ptr<core::ui::MacroView> macro_view_;
    std::unique_ptr<core::ui::SequencerView> sequencer_view_;
    std::unique_ptr<core::ui::TransportBar> transport_bar_;

    // Overlay system
    std::unique_ptr<oc::context::OverlayManager<core::ui::OverlayType>> overlay_controller_;
    std::unique_ptr<ms::ui::StringListSelector> view_selector_;
    oc::state::SignalWatcher view_selector_watcher_;
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> macro_edit_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> macro_edit_selector_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> macro_page_selector_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> macro_target_selector_overlay_;
    oc::state::SignalWatcher macro_edit_watcher_;
    oc::state::SignalWatcher macro_edit_selector_watcher_;
    oc::state::SignalWatcher macro_page_selector_watcher_;
    oc::state::SignalWatcher macro_target_selector_watcher_;
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> seq_pattern_config_overlay_;
    oc::state::SignalWatcher seq_pattern_config_watcher_;
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> seq_step_edit_overlay_;
    oc::state::SignalWatcher seq_step_edit_watcher_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> seq_property_selector_overlay_;
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> global_settings_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> global_settings_selector_overlay_;
    oc::state::SignalWatcher seq_property_selector_watcher_;
    oc::state::SignalWatcher global_settings_watcher_;
    oc::state::SignalWatcher global_settings_selector_watcher_;
    oc::state::SignalWatcher seq_macro_encoder_watcher_;
    oc::state::SignalWatcher active_view_watcher_;

    // Handlers
    std::unique_ptr<core::handler::MacroValueHandler> input_handler_;
    std::unique_ptr<core::handler::MacroMidiHandler> midi_handler_;
    std::unique_ptr<core::handler::TransportHandler> transport_handler_;
    std::unique_ptr<core::handler::SequencerStepHandler> sequencer_step_handler_;
    std::unique_ptr<core::handler::SequencerPatternConfigHandler> sequencer_pattern_config_handler_;
    std::unique_ptr<core::handler::SequencerStepEditHandler> sequencer_step_edit_handler_;
    std::unique_ptr<core::handler::SequencerPropertySelectorHandler> sequencer_property_selector_handler_;
    std::unique_ptr<core::handler::SequencerMacroPropertyHandler> sequencer_macro_property_handler_;
    std::unique_ptr<core::handler::ViewSwitcherHandler> view_switcher_handler_;
    std::unique_ptr<core::handler::MacroEditHandler> macro_edit_handler_;
    std::unique_ptr<core::handler::GlobalSettingsHandler> global_settings_handler_;

    // Global services (not tied to a view scope)
    std::unique_ptr<core::sequencer::MidiClockSyncService> midi_clock_sync_;
    std::unique_ptr<core::sequencer::SequencerPlaybackService> sequencer_playback_;

    // Cached encoder configuration (avoid resetting quantization every sync)
    uint8_t seq_macro_steps_configured_ = 0;
    uint8_t seq_opt_steps_configured_ = 0;
    std::array<float, core::state::MACRO_COUNT> seq_macro_position_cache_{};
    std::array<bool, core::state::MACRO_COUNT> seq_macro_position_valid_{};
    float seq_opt_position_cache_ = 0.0f;
    bool seq_opt_position_valid_ = false;
};

}  // namespace core::context
