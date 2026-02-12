#include "StandaloneContext.hpp"

#include <string>
#include <vector>

#include <lvgl.h>

#include <oc/log/Log.hpp>
#include <oc/interface/IEncoder.hpp>
#include <oc/ui/lvgl/FontLoader.hpp>
#include <oc/ui/lvgl/Screen.hpp>
#include <oc/time/Time.hpp>

#include <config/App.hpp>
#include "handler/macro/MacroEditHandler.hpp"
#include "handler/macro/MacroMidiHandler.hpp"
#include "handler/macro/MacroValueHandler.hpp"
#include "handler/sequencer/SequencerStepHandler.hpp"
#include "handler/transport/TransportHandler.hpp"
#include "handler/view/ViewSwitcherHandler.hpp"
#include <ms/ui/font/CoreFonts.hpp>
#include <ms/ui/widget/StringListSelector.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/macro/MacroEditOverlay.hpp"
#include <oc/context/OverlayManager.hpp>
#include "ui/transportbar/TransportBar.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/view/SequencerView.hpp"
#include <ms/ui/ViewContainer.hpp>

#include "sequencer/SequencerPlaybackService.hpp"

namespace core::context {

// Constructor and destructor must be in .cpp where handler types are complete
StandaloneContext::StandaloneContext(core::state::CoreState& state) : core_state_(state) {}

StandaloneContext::~StandaloneContext() = default;

// =============================================================================
// IContext Lifecycle
// =============================================================================

oc::type::Result<void> StandaloneContext::init() {
    OC_LOG_INFO("StandaloneContext::initialize()");

    oc::ui::lvgl::font::load(CORE_FONT_ENTRIES, CORE_FONT_COUNT);
    oc::ui::lvgl::font::load(STANDALONE_FONT_ENTRIES, STANDALONE_FONT_COUNT);
    linkCoreFontAliases();

    // Configure special encoders once (avoid hidden handler coupling)
    encoders().setMode(Config::EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);

    // Sync encoder positions with restored values BEFORE creating handlers
    syncEncodersFromState();

    // Create UI container with zones
    view_container_ = std::make_unique<ms::ui::ViewContainer>(oc::ui::lvgl::Screen::root());
    lv_obj_t* mainZone = view_container_->getMainZone();

    // Create views (activate via activeView signal)
    macro_view_ = std::make_unique<core::ui::MacroView>(mainZone, core_state_);
    sequencer_view_ = std::make_unique<core::ui::SequencerView>(mainZone, core_state_);
    setupActiveViewSwitching();
    applyActiveView();

    // Create TransportBar in bottom zone
    transport_bar_ = std::make_unique<core::ui::TransportBar>(
        view_container_->getBottomZone(),
        core_state_.statusBar
    );

    // Create overlay manager with AuthorityResolver
    overlay_controller_ = std::make_unique<oc::context::OverlayManager<core::ui::OverlayType>>(
        core_state_.overlays, buttons()
    );

    // Global overlay: ViewSelector (parent = mainZone so it covers views but not TransportBar)
    view_selector_ = std::make_unique<ms::ui::StringListSelector>(mainZone);
    view_selector_->setTitle("Select View");
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::VIEW_SELECTOR,
        reinterpret_cast<oc::type::ScopeID>(view_selector_->getElement()),
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP)
    );
    setupViewSelectorRendering();

    // Create MacroEdit overlay (parented to MacroView)
    macro_edit_overlay_ = std::make_unique<core::ui::MacroEditOverlay>(macro_view_->getElement());

    // Register overlay cleanup
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::MACRO_EDIT,
        reinterpret_cast<oc::type::ScopeID>(macro_edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)  // No latch button
    );

    // Setup rendering subscriptions for MacroEditOverlay (orchestrator pattern)
    setupMacroEditRendering();

    // Create handlers (bindings scoped to view element)
    input_handler_ = std::make_unique<core::handler::MacroValueHandler>(
        core_state_, encoders(), midi(), macro_view_->getElement()
    );

    // Sequencer input handler (scoped to SequencerView)
    sequencer_step_handler_ = std::make_unique<core::handler::SequencerStepHandler>(
        core_state_, encoders(), buttons(), sequencer_view_->getElement()
    );

    midi_handler_ = std::make_unique<core::handler::MacroMidiHandler>(
        core_state_, encoders()
    );

    // MIDI input is routed through the framework EventBus (never via MidiAPI callbacks)
    onMidiCC([this](uint8_t ch, uint8_t cc, uint8_t val) {
        if (midi_handler_) midi_handler_->onCC(ch, cc, val);
    });
    onMidiNoteOn([this](uint8_t, uint8_t, uint8_t) {
        if (midi_handler_) midi_handler_->onNoteIn();
    });
    onMidiNoteOff([this](uint8_t, uint8_t, uint8_t) {
        if (midi_handler_) midi_handler_->onNoteIn();
    });
    transport_handler_ = std::make_unique<core::handler::TransportHandler>(
        core_state_, encoders(), buttons(), mainZone
    );

    // View selector handler (LEFT_TOP + NAV)
    {
        using OverlayCtx = ms::ui::OverlayBindingContext<core::ui::OverlayType>;
        OverlayCtx ctx{*overlay_controller_, mainZone, view_selector_->getElement()};
        view_switcher_handler_ = std::make_unique<core::handler::ViewSwitcherHandler>(
            core_state_, ctx, encoders(), buttons()
        );
    }

    // Create MacroEdit input handler (two-level scoping)
    macro_edit_handler_ = std::make_unique<core::handler::MacroEditHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        macro_view_->getElement(),              // MacroView scope (open trigger)
        macro_edit_overlay_->getElement()   // Overlay scope (edit/close)
    );

    // Global services (not tied to any view scope)
    sequencer_playback_ = std::make_unique<core::sequencer::SequencerPlaybackService>(
        core_state_.sequencer,
        core_state_.statusBar,
        midi()
    );

    view_container_->show();

    OC_LOG_INFO("StandaloneContext ready");
    return oc::type::Result<void>::ok();
}

void StandaloneContext::update() {
    if (sequencer_playback_) {
        sequencer_playback_->update(oc::time::millis());
    }
}

void StandaloneContext::onCleanup() {
    // Ensure overlay stack is reset while UI objects are still alive.
    // CoreState survives context switches.
    if (overlay_controller_) {
        overlay_controller_->hideAll();
    } else {
        core_state_.overlays.hideAll();
    }
    core_state_.macroEdit.reset();

    if (sequencer_playback_) {
        sequencer_playback_->stop();
        sequencer_playback_.reset();
    }

    // Handlers first (they reference state/APIs)
    macro_edit_handler_.reset();
    view_switcher_handler_.reset();
    sequencer_step_handler_.reset();
    transport_handler_.reset();
    input_handler_.reset();
    midi_handler_.reset();

    // SignalWatcher cleanup is automatic (RAII)

    // Overlay UI
    macro_edit_overlay_.reset();
    view_selector_.reset();

    // Overlay controller (clears authority resolver)
    overlay_controller_.reset();

    // TransportBar (TopBar is now managed by MacroView)
    transport_bar_.reset();

    if (macro_view_) {
        macro_view_->onDeactivate();
        macro_view_.reset();
    }
    if (sequencer_view_) {
        sequencer_view_->onDeactivate();
        sequencer_view_.reset();
    }

    view_container_.reset();
}

// =============================================================================
// Private Methods
// =============================================================================

void StandaloneContext::syncEncodersFromState() {
    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        float value = core_state_.macros.slots[i].value.get();
        encoders().setPosition(Config::MACRO_ENCODERS[i], value);
    }
    OC_LOG_DEBUG("Synced encoder positions from restored state");
}

void StandaloneContext::setupMacroEditRendering() {
    macro_edit_watcher_.watchAll(
        [this]() { renderMacroEdit(); },
        core_state_.macroEdit.visible,
        core_state_.macroEdit.editingIndex,
        core_state_.macroEdit.tempChannel,
        core_state_.macroEdit.tempCC,
        core_state_.macroEdit.focusedRow
    );
}

void StandaloneContext::renderMacroEdit() {
    macro_edit_overlay_->render({
        .editingIndex = core_state_.macroEdit.editingIndex.get(),
        .channel = core_state_.macroEdit.tempChannel.get(),
        .cc = core_state_.macroEdit.tempCC.get(),
        .focusedRow = core_state_.macroEdit.focusedRow.get(),
        .visible = core_state_.macroEdit.visible.get()
    });
}

void StandaloneContext::setupViewSelectorRendering() {
    view_selector_watcher_.watchAll(
        [this]() { renderViewSelector(); },
        core_state_.viewSelector.visible,
        core_state_.viewSelector.selectedIndex
    );
}

void StandaloneContext::renderViewSelector() {
    if (!view_selector_) return;
    static const std::vector<std::string> VIEW_NAMES = {"Macros", "Sequencer"};

    view_selector_->render({
        .items = &VIEW_NAMES,
        .selectedIndex = core_state_.viewSelector.selectedIndex.get(),
        .visible = core_state_.viewSelector.visible.get()
    });
}

void StandaloneContext::setupActiveViewSwitching() {
    active_view_watcher_.watchAll(
        [this]() { applyActiveView(); },
        core_state_.activeView
    );
}

void StandaloneContext::applyActiveView() {
    if (macro_view_) macro_view_->onDeactivate();
    if (sequencer_view_) sequencer_view_->onDeactivate();

    switch (core_state_.activeView.get()) {
        case core::ui::ViewType::SEQUENCER:
            if (sequencer_view_) sequencer_view_->onActivate();
            break;
        case core::ui::ViewType::MACRO:
        default:
            if (macro_view_) macro_view_->onActivate();
            break;
    }
}

}  // namespace core::context
